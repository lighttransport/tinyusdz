// SPDX-License-Identifier: Apache-2.0
#include "lightrt_mtlx_bridge.hh"
#include "rt_scene_build.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

bool Near(float a, float b, float eps = 1.0e-6f) {
  return std::fabs(a - b) <= eps;
}

void Identity(float m[16]) {
  for (int i = 0; i < 16; ++i) m[i] = 0.0f;
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

tusdview::DrawMaterialParamCPU FloatParam(const char* name, float v) {
  tusdview::DrawMaterialParamCPU p;
  p.shader = "OpenPBRSurface";
  p.name = name;
  p.type = tusdview::DrawMaterialParamType::Float;
  p.value[0] = v;
  return p;
}

tusdview::DrawMaterialParamCPU Vec3Param(const char* name, float x, float y,
                                         float z) {
  tusdview::DrawMaterialParamCPU p;
  p.shader = "OpenPBRSurface";
  p.name = name;
  p.type = tusdview::DrawMaterialParamType::Vec3;
  p.value[0] = x;
  p.value[1] = y;
  p.value[2] = z;
  p.value[3] = 1.0f;
  return p;
}

}  // namespace

int main() {
  tusdview::DrawScene scene;

  tusdview::DrawMaterialCPU mat;
  mat.name = "OpenPBRBridge";
  mat.hasOpenPBRSurface = true;
  mat.params.push_back(FloatParam("base_weight", 0.75f));
  mat.params.push_back(Vec3Param("base_color", 0.2f, 0.4f, 0.6f));
  mat.params.push_back(FloatParam("base_metalness", 0.5f));
  mat.params.push_back(FloatParam("specular_weight", 0.25f));
  mat.params.push_back(FloatParam("specular_roughness", 0.35f));
  mat.params.push_back(FloatParam("specular_ior", 1.6f));
  mat.params.push_back(FloatParam("coat_weight", 0.3f));
  mat.params.push_back(Vec3Param("coat_color", 0.7f, 0.8f, 0.9f));
  mat.params.push_back(FloatParam("coat_roughness", 0.2f));
  mat.params.push_back(FloatParam("coat_ior", 1.4f));
  mat.params.push_back(FloatParam("emission_luminance", 2.0f));
  mat.params.push_back(Vec3Param("emission_color", 0.1f, 0.2f, 0.3f));
  mat.params.push_back(FloatParam("opacity", 0.65f));
  tusdview::BakeRealtimePbrMaterial(&mat);
  mat.occlusion = 0.65f;
  mat.baseColorSample.uv = {1.0f, 0.1f, 0.2f, 0.9f, 0.3f, 0.4f};
  mat.metallicSample.uv = {0.5f, 0.0f, 0.0f, 0.5f, 0.1f, 0.2f};
  mat.roughnessSample.uv = mat.metallicSample.uv;
  mat.normalSample.uv = {1.0f, 0.0f, 0.0f, 1.0f, 0.7f, 0.8f};
  mat.emissiveSample.uv = {0.25f, 0.0f, 0.0f, 0.25f, 0.2f, 0.3f};
  mat.displacementUv = {2.0f, 0.0f, 0.0f, 2.0f, -0.5f, 0.5f};
  mat.displacementTex = 11;
  mat.displacementSample.isUdim = true;
  mat.baseColorSample.scale[0] = 0.25f;
  mat.baseColorSample.bias[0] = 0.10f;
  mat.normalSample.scale[1] = 2.0f;
  mat.normalSample.bias[1] = -1.0f;
  mat.emissiveSample.scale[2] = 3.0f;
  mat.emissiveSample.bias[2] = 0.30f;
  mat.metallicChannel = 0;
  mat.roughnessChannel = 3;
  mat.metallicTexScale = 0.6f;
  mat.metallicTexBias = 0.05f;
  mat.roughnessTexScale = 0.7f;
  mat.roughnessTexBias = 0.15f;
  mat.displacementTexScale = 1.25f;
  mat.displacementTexBias = -0.25f;
  mat.opacityTex = 7;
  mat.opacityChannel = 2;
  mat.opacityTexScale = 0.8f;
  mat.opacityTexBias = 0.1f;
  mat.baseColorSample.uvSet = 1;
  mat.metallicSample.uvSet = 1;
  mat.normalSample.uvSet = 1;
  mat.emissiveSample.uvSet = 1;
  mat.opacitySample.uvSet = 1;
  mat.opacitySample.uv.m00 = 1.5f;
  mat.opacitySample.uv.tx = 0.25f;
  mat.roughnessSample.uvSet = 1;
  mat.occlusionTex = 9;
  mat.occlusionChannel = 1;
  mat.occlusionTexScale = 0.75f;
  mat.occlusionTexBias = 0.125f;
  mat.occlusionSample.uvSet = 1;
  mat.occlusionSample.uv.m00 = 0.5f;
  mat.occlusionSample.uv.ty = 0.375f;
  mat.specularColorSample.scale[0] = 0.51f;
  mat.specularColorSample.bias[2] = 0.12f;
  mat.specularColorSample.uv.m00 = 0.91f;
  mat.specularColorSample.uv.ty = 0.19f;
  mat.specularColorSample.uvSet = 1;
  mat.specularColorSample.isPtex = true;
  mat.specularColorSample.ptexRectTexelOffset = 101;
  mat.specularColorSample.ptexFaceCount = 7;
  mat.useSpecularWorkflow = true;
  mat.coatWeightSample.scale[1] = 0.61f;
  mat.coatWeightSample.bias[1] = 0.13f;
  mat.coatWeightSample.uvSet = 1;
  mat.coatWeightSample.isPtex = true;
  mat.coatWeightSample.ptexRectTexelOffset = 202;
  mat.coatWeightSample.ptexFaceCount = 8;
  mat.coatColorSample.scale[2] = 0.71f;
  mat.coatColorSample.bias[0] = 0.14f;
  mat.coatColorSample.uvSet = 1;
  mat.coatColorSample.isPtex = true;
  mat.coatColorSample.ptexRectTexelOffset = 303;
  mat.coatColorSample.ptexFaceCount = 9;
  mat.coatRoughnessSample.scale[3] = 0.81f;
  mat.coatRoughnessSample.bias[3] = 0.15f;
  mat.coatRoughnessSample.uvSet = 1;
  mat.coatRoughnessSample.isPtex = true;
  mat.coatRoughnessSample.ptexRectTexelOffset = 404;
  mat.coatRoughnessSample.ptexFaceCount = 10;
  mat.coatNormalSample.uv.m00 = 0.83f;
  mat.coatNormalSample.uv.ty = 0.23f;
  mat.coatNormalSample.uvSet = 1;
  mat.coatNormalSample.scale[0] = 1.7f;
  mat.coatNormalSample.bias[1] = -0.4f;
  mat.alphaMode = static_cast<int>(tusdview::AlphaMode::Mask);
  mat.alphaCutoff = 0.42f;
  std::vector<float> directPack(tusdview::kLightRtOpenPBRFloats, -1.0f);
  tusdview::PackLightRtOpenPBR(mat, directPack.data());
  std::vector<float> directTexPack(tusdview::kRtMaterialTextureParamFloats,
                                   -1.0f);
  tusdview::PackRtMaterialTextureParams(mat, directTexPack.data());
  std::vector<float> directRasterTexPack(
      tusdview::kRasterMaterialTextureParamFloats, -1.0f);
  tusdview::PackRasterMaterialTextureParams(mat, directRasterTexPack.data());
  scene.materials.push_back(mat);

  tusdview::DrawMeshCPU mesh;
  mesh.name = "tri";
  Identity(mesh.world);
  Identity(mesh.skinGeomBind);
  mesh.vertices.resize(3);
  mesh.vertices[0].px = 0.0f; mesh.vertices[0].py = 0.0f; mesh.vertices[0].pz = 0.0f;
  mesh.vertices[1].px = 1.0f; mesh.vertices[1].py = 0.0f; mesh.vertices[1].pz = 0.0f;
  mesh.vertices[2].px = 0.0f; mesh.vertices[2].py = 1.0f; mesh.vertices[2].pz = 0.0f;
  for (tusdview::DrawVertex& v : mesh.vertices) {
    v.nz = 1.0f;
  }
  mesh.indices = {0, 1, 2};
  mesh.vertexAlpha = {0.25f, 0.5f, 0.75f};
  tusdview::DrawSubmesh sub;
  sub.indexOffset = 0;
  sub.indexCount = 3;
  sub.materialId = 0;
  mesh.submeshes.push_back(sub);
  scene.meshes.push_back(mesh);

  tusdview::HostScene host;
  std::string err;
  if (!tusdview::BuildHostScene(scene, 0, 0, 0.0f, &host, &err)) {
    std::fprintf(stderr, "BuildHostScene failed: %s\n", err.c_str());
    return 1;
  }

  // CUDA and HIP upload HostScene::mat verbatim. Keep a focused assertion that
  // two GeomSubset-style EBO ranges survive BVH leaf reordering as two distinct
  // per-triangle material ids.
  {
    tusdview::DrawScene subsetScene;
    subsetScene.materials.resize(2);
    tusdview::DrawMeshCPU subsetMesh = mesh;
    subsetMesh.vertices.resize(6);
    for (size_t i = 0; i < 3; ++i) {
      subsetMesh.vertices[i + 3] = subsetMesh.vertices[i];
      subsetMesh.vertices[i + 3].px += 2.0f;
    }
    subsetMesh.indices = {0, 1, 2, 3, 4, 5};
    subsetMesh.vertexAlpha.clear();
    subsetMesh.submeshes.clear();
    tusdview::DrawSubmesh left;
    left.indexCount = 3;
    left.materialId = 0;
    subsetMesh.submeshes.push_back(left);
    tusdview::DrawSubmesh right;
    right.indexOffset = 3;
    right.indexCount = 3;
    right.materialId = 1;
    subsetMesh.submeshes.push_back(right);
    subsetScene.meshes.push_back(std::move(subsetMesh));

    tusdview::HostScene subsetHost;
    std::string subsetErr;
    if (!tusdview::BuildHostScene(subsetScene, 0, 0, 0.0f, &subsetHost,
                                  &subsetErr)) {
      std::fprintf(stderr, "GeomSubset HostScene build failed: %s\n",
                   subsetErr.c_str());
      return 1;
    }
    std::sort(subsetHost.mat.begin(), subsetHost.mat.end());
    if (subsetHost.mat != std::vector<int>({0, 1})) {
      std::fprintf(stderr,
                   "GeomSubset material ids were not preserved per triangle\n");
      return 1;
    }
  }
  if (host.matLightRt.size() < tusdview::kLightRtOpenPBRFloats) {
    std::fprintf(stderr, "matLightRt was not packed\n");
    return 1;
  }

  const float* p = host.matLightRt.data();
  for (int i = 0; i < tusdview::kLightRtOpenPBRFloats; ++i) {
    if (!Near(p[i], directPack[static_cast<size_t>(i)])) {
      std::fprintf(stderr, "HostScene pack drift at lane %d\n", i);
      return 1;
    }
  }
  if (!Near(p[0], 0.2f) || !Near(p[1], 0.4f) || !Near(p[2], 0.6f) ||
      !Near(p[3], 0.75f) || !Near(p[7], 0.25f) || !Near(p[27], 0.3f) ||
      !Near(p[35], 2.0f) || !Near(p[39], 0.65f) || !Near(p[40], 0.5f) ||
      !Near(p[42], 0.35f) || !Near(p[43], 1.6f) || !Near(p[44], 0.2f) ||
      !Near(p[51], 1.0f) || !Near(p[54], 1.0f) || !Near(p[55], 0.42f)) {
    std::fprintf(stderr, "unexpected LightRT/OpenPBR packing\n");
    return 1;
  }
  if (host.matTexParam.size() < tusdview::kRtMaterialTextureParamFloats) {
    std::fprintf(stderr, "matTexParam was not packed\n");
    return 1;
  }
  for (int i = 0; i < tusdview::kRtMaterialTextureParamFloats; ++i) {
    if (!Near(host.matTexParam[static_cast<size_t>(i)],
              directTexPack[static_cast<size_t>(i)])) {
      std::fprintf(stderr, "HostScene texture-param pack drift at lane %d\n", i);
      return 1;
    }
  }
  if (!Near(directTexPack[0], 1.0f) || !Near(directTexPack[2], 0.3f) ||
      !Near(directTexPack[5], 0.4f) || !Near(directTexPack[44], 1.0f) ||
      !Near(directTexPack[45], 2.0f) || !Near(directTexPack[49], -1.0f) ||
      !Near(directTexPack[60], 0.0f) || !Near(directTexPack[63], 3.0f) ||
      !Near(directTexPack[61], 0.6f) || !Near(directTexPack[65], 0.15f) ||
      !Near(directTexPack[66], 2.0f) || !Near(directTexPack[68], 0.1f) ||
      !Near(directTexPack[101], 0.61f) || !Near(directTexPack[105], 0.13f) ||
      !Near(directTexPack[110], 0.71f) || !Near(directTexPack[112], 0.14f) ||
      !Near(directTexPack[119], 0.81f) || !Near(directTexPack[123], 0.15f) ||
      !Near(directTexPack[124], 0.91f) || !Near(directTexPack[129], 0.19f) ||
      !Near(directTexPack[130], 1.0f) || !Near(directTexPack[131], 1.0f) ||
      !Near(directTexPack[132], 0.51f) || !Near(directTexPack[138], 0.12f) ||
      !Near(directTexPack[140], 0.83f) || !Near(directTexPack[145], 0.23f) ||
      !Near(directTexPack[146], 1.0f) || !Near(directTexPack[147], 1.7f) ||
      !Near(directTexPack[152], -0.4f)) {
    std::fprintf(stderr, "unexpected RT texture-param packing\n");
    return 1;
  }
  if (!Near(directRasterTexPack[63 * 4 + 0], 101.0f) ||
      !Near(directRasterTexPack[63 * 4 + 1], 7.0f) ||
      !Near(directRasterTexPack[64 * 4 + 0], 202.0f) ||
      !Near(directRasterTexPack[65 * 4 + 1], 9.0f) ||
      !Near(directRasterTexPack[66 * 4 + 0], 404.0f)) {
    std::fprintf(stderr, "advanced raster Ptex metadata was not packed\n");
    return 1;
  }
  if (!Near(directRasterTexPack[8 * 4 + 0], 2.0f) ||
      !Near(directRasterTexPack[8 * 4 + 6], 0.5f) ||
      !Near(directRasterTexPack[16 * 4 + 0], 0.0f) ||
      !Near(directRasterTexPack[16 * 4 + 1], 3.0f) ||
      !Near(directRasterTexPack[17 * 4 + 2], 1.25f) ||
      !Near(directRasterTexPack[17 * 4 + 3], -0.25f) ||
      !Near(directRasterTexPack[18 * 4 + 0], 1.0f) ||
      !Near(directRasterTexPack[18 * 4 + 1], 1.0f) ||
      !Near(directRasterTexPack[18 * 4 + 2], 1.0f) ||
      !Near(directRasterTexPack[18 * 4 + 3], 1.0f) ||
      !Near(directRasterTexPack[20 * 4 + 0], 1.5f) ||
      !Near(directRasterTexPack[20 * 4 + 2], 0.25f) ||
      !Near(directRasterTexPack[22 * 4 + 0], 2.0f) ||
      !Near(directRasterTexPack[22 * 4 + 1], 0.8f) ||
      !Near(directRasterTexPack[22 * 4 + 2], 0.1f) ||
      !Near(directRasterTexPack[22 * 4 + 3], 1.0f) ||  // opacity
      !Near(directRasterTexPack[24 * 4 + 0], 7.0f) ||
      !Near(directRasterTexPack[27 * 4 + 0], 0.3f) ||
      !Near(directRasterTexPack[27 * 4 + 1], 0.2f) ||
      !Near(directRasterTexPack[27 * 4 + 2], 1.4f) ||
      !Near(directRasterTexPack[27 * 4 + 3], 0.65f) ||
      !Near(directRasterTexPack[28 * 4 + 0], 0.7f) ||
      !Near(directRasterTexPack[28 * 4 + 1], 0.8f) ||
      !Near(directRasterTexPack[28 * 4 + 2], 0.9f) ||
      !Near(directRasterTexPack[24 * 4 + 2], 9.0f) ||
      !Near(directRasterTexPack[25 * 4 + 3], 1.0f) ||  // roughness uvSet
      !Near(directRasterTexPack[29 * 4 + 0], 0.5f) ||
      !Near(directRasterTexPack[30 * 4 + 2], 0.375f) ||
      !Near(directRasterTexPack[31 * 4 + 0], 1.0f) ||
      !Near(directRasterTexPack[31 * 4 + 1], 0.75f) ||
      !Near(directRasterTexPack[31 * 4 + 2], 0.125f) ||
      !Near(directRasterTexPack[31 * 4 + 3], 1.0f) ||  // occlusion uvSet
      !Near(directRasterTexPack[40 * 4 + 2], 1.0f) ||  // coatWeight uvSet
      !Near(directRasterTexPack[40 * 4 + 3], 1.0f) ||  // coatRoughness uvSet
      !Near(directRasterTexPack[41 * 4 + 0], 1.0f) ||  // specularColor uvSet
      !Near(directRasterTexPack[41 * 4 + 1], 1.0f) ||  // coatColor uvSet
      !Near(directRasterTexPack[42 * 4 + 0], 0.51f) ||
      !Near(directRasterTexPack[43 * 4 + 2], 0.12f) ||
      !Near(directRasterTexPack[44 * 4 + 1], 0.61f) ||
      !Near(directRasterTexPack[45 * 4 + 1], 0.13f) ||
      !Near(directRasterTexPack[46 * 4 + 2], 0.71f) ||
      !Near(directRasterTexPack[47 * 4 + 0], 0.14f) ||
      !Near(directRasterTexPack[48 * 4 + 3], 0.81f) ||
      !Near(directRasterTexPack[49 * 4 + 3], 0.15f) ||
      !Near(directRasterTexPack[50 * 4 + 0], 0.83f) ||
      !Near(directRasterTexPack[51 * 4 + 2], 0.23f) ||
      !Near(directRasterTexPack[52 * 4 + 0], 1.7f) ||
      !Near(directRasterTexPack[53 * 4 + 1], -0.4f) ||
      !Near(directRasterTexPack[53 * 4 + 3], 1.0f) ||
      !Near(directRasterTexPack[55 * 4 + 1], 11.0f)) {
    std::fprintf(stderr, "unexpected raster texture-param packing\n");
    return 1;
  }
  if (host.cols.size() != 12 || !Near(host.cols[3], 0.25f) ||
      !Near(host.cols[7], 0.5f) || !Near(host.cols[11], 0.75f)) {
    std::fprintf(stderr, "displayOpacity RGBA packing changed\n");
    return 1;
  }

  // The shared CUDA/HIP host scene keeps a compact optional back-material
  // stream. Also lock the legacy convention that material index 0 may be a
  // real authored material; only the next loader's anonymous index-0 record is
  // suppressed for an unbound instanced prototype.
  tusdview::DrawScene sidedScene;
  tusdview::DrawMaterialCPU frontMat;
  frontMat.name = "AuthoredFrontAtZero";
  frontMat.baseColor[0] = 0.9f;
  frontMat.baseColor[1] = 0.1f;
  frontMat.baseColor[2] = 0.2f;
  tusdview::DrawMaterialCPU backMat;
  backMat.name = "AuthoredBack";
  backMat.baseColor[0] = 0.1f;
  backMat.baseColor[1] = 0.2f;
  backMat.baseColor[2] = 0.9f;
  sidedScene.materials = {frontMat, backMat};
  tusdview::DrawMeshCPU sidedMesh = mesh;
  sidedMesh.submeshes[0].backfaceMaterialId = 1;
  sidedMesh.instanceXforms = {1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f};
  sidedScene.meshes.push_back(sidedMesh);
  tusdview::HostScene sidedHost;
  if (!tusdview::BuildHostScene(sidedScene, 0, 0, 0.0f, &sidedHost, &err) ||
      sidedHost.mat.size() != 1 || sidedHost.mat[0] != 0 ||
      sidedHost.backMat.size() != 1 || sidedHost.backMat[0] != 1 ||
      sidedHost.matBase.size() != 6 || !Near(sidedHost.matBase[0], 0.9f) ||
      !Near(sidedHost.matBase[5], 0.9f) || sidedHost.instances.size() != 1 ||
      !Near(sidedHost.instances[0].tint[0], 1.0f)) {
    std::fprintf(stderr, "front/back RT material packing changed: %s\n",
                 err.c_str());
    return 1;
  }

  tusdview::DrawScene placeholderScene;
  placeholderScene.materials.emplace_back();  // anonymous next-loader fallback
  tusdview::DrawMeshCPU placeholderMesh = sidedMesh;
  placeholderMesh.submeshes[0].backfaceMaterialId = -1;
  placeholderScene.meshes.push_back(std::move(placeholderMesh));
  tusdview::HostScene placeholderHost;
  if (!tusdview::BuildHostScene(placeholderScene, 0, 0, 0.0f,
                                &placeholderHost, &err) ||
      placeholderHost.mat.size() != 1 || placeholderHost.mat[0] != -1 ||
      !placeholderHost.backMat.empty() || placeholderHost.instances.size() != 1 ||
      !Near(placeholderHost.instances[0].tint[0], sidedMesh.flatColor[0])) {
    std::fprintf(stderr, "anonymous RT material fallback changed: %s\n",
                 err.c_str());
    return 1;
  }

  tusdview::DrawMaterialCPU defaultMat;
  std::vector<float> defaultPack(tusdview::kLightRtOpenPBRFloats, -1.0f);
  tusdview::PackLightRtOpenPBR(defaultMat, defaultPack.data());
  if (!Near(defaultPack[0], 0.8f) || !Near(defaultPack[3], 1.0f) ||
      !Near(defaultPack[39], 1.0f) || !Near(defaultPack[51], 0.0f) ||
      !Near(defaultPack[54], 0.0f) || !Near(defaultPack[55], 0.5f)) {
    std::fprintf(stderr, "unexpected default LightRT/OpenPBR packing\n");
    return 1;
  }
  std::vector<float> sharedDefaultPack(tusdview::kLightRtOpenPBRFloats, -2.0f);
  tinyusdz::tydra::RealtimePbrMaterial sharedDefault;
  tinyusdz::tydra::PackRealtimePbrMaterial(
      sharedDefault, false, 0.0f, 0.5f, sharedDefaultPack.data());
  for (int i = 0; i < tusdview::kLightRtOpenPBRFloats; ++i) {
    if (!Near(defaultPack[size_t(i)], sharedDefaultPack[size_t(i)])) {
      std::fprintf(stderr, "shared/default realtime-PBR pack mismatch at %d\n", i);
      return 1;
    }
  }

  tusdview::DrawMaterialCPU neutralNormalMat;
  neutralNormalMat.hasOpenPBRSurface = true;
  neutralNormalMat.params.push_back(Vec3Param("normal", 0.0f, 0.0f, 1.0f));
  neutralNormalMat.params.push_back(Vec3Param("coat_normal", 0.0f, 0.0f, 1.0f));
  tusdview::BakeLightRtOpenPBR(&neutralNormalMat);
  if (neutralNormalMat.lightRtOpenPBR.hasNormalInput) {
    std::fprintf(stderr, "neutral OpenPBR normals were marked active\n");
    return 1;
  }

  tusdview::DrawMaterialCPU customCoatNormalMat;
  customCoatNormalMat.hasOpenPBRSurface = true;
  customCoatNormalMat.params.push_back(Vec3Param("normal", 0.0f, 0.0f, 1.0f));
  customCoatNormalMat.params.push_back(
      Vec3Param("coat_normal", 0.25f, 0.0f, 0.97f));
  tusdview::BakeLightRtOpenPBR(&customCoatNormalMat);
  if (!customCoatNormalMat.lightRtOpenPBR.hasNormalInput) {
    std::fprintf(stderr, "custom OpenPBR coat normal was not marked active\n");
    return 1;
  }

  const char* graphXml =
      "<materialx version=\"1.38\">"
      "  <multiply name=\"Tint\" type=\"color3\">"
      "    <input name=\"in1\" type=\"color3\" value=\"0.2, 0.4, 0.6\"/>"
      "    <input name=\"in2\" type=\"color3\" value=\"2.0, 0.5, 0.25\"/>"
      "  </multiply>"
      "  <open_pbr_surface name=\"GraphSurface\" type=\"surfaceshader\">"
      "    <input name=\"base_color\" type=\"color3\" nodename=\"Tint\"/>"
      "    <input name=\"base_diffuse_roughness\" type=\"float\" value=\"0.12\"/>"
      "    <input name=\"base_metalness\" type=\"float\" value=\"0.45\"/>"
      "    <input name=\"base_roughness\" type=\"float\" value=\"0.25\"/>"
      "    <input name=\"emission_luminance\" type=\"float\" value=\"1.5\"/>"
      "    <input name=\"geometry_opacity\" type=\"float\" value=\"0.8\"/>"
      "  </open_pbr_surface>"
      "  <surfacematerial name=\"GraphMat\">"
      "    <input name=\"surfaceshader\" type=\"surfaceshader\""
      "           nodename=\"GraphSurface\"/>"
      "  </surfacematerial>"
      "</materialx>";
  tusdview::tydra::LightRtOpenPBRParams graphEval;
  std::string graphErr;
  if (!tusdview::EvaluateMaterialXStringToLightRtOpenPBR(
          graphXml, "GraphMat", &graphEval, &graphErr)) {
    std::fprintf(stderr, "MaterialX graph eval failed: %s\n", graphErr.c_str());
    return 1;
  }
  if (!Near(graphEval.baseColor[0], 0.4f) ||
      !Near(graphEval.baseColor[1], 0.2f) ||
      !Near(graphEval.baseColor[2], 0.15f) ||
      !Near(graphEval.diffuseRoughness, 0.12f) ||
      !Near(graphEval.metalness, 0.45f) ||
      !Near(graphEval.specularRoughness, 0.25f) ||
      !Near(graphEval.emission, 1.5f) ||
      !Near(graphEval.opacity, 0.8f)) {
    std::fprintf(stderr, "MaterialX graph values were not evaluated\n");
    return 1;
  }

  const char* standardGraphXml =
      "<materialx version=\"1.38\">"
      "  <nodegraph name=\"NG_standard\">"
      "    <combine3 name=\"packed\" type=\"color3\">"
      "      <input name=\"in1\" type=\"float\" value=\"0.2\"/>"
      "      <input name=\"in2\" type=\"float\" value=\"0.4\"/>"
      "      <input name=\"in3\" type=\"float\" value=\"0.6\"/>"
      "    </combine3>"
      "    <extract name=\"green\" type=\"float\">"
      "      <input name=\"in\" type=\"color3\" nodename=\"packed\"/>"
      "      <input name=\"index\" type=\"integer\" value=\"1\"/>"
      "    </extract>"
      "    <convert name=\"green_color\" type=\"color3\">"
      "      <input name=\"in\" type=\"float\" nodename=\"green\"/>"
      "    </convert>"
      "    <add name=\"base\" type=\"color3\">"
      "      <input name=\"in1\" type=\"color3\" nodename=\"green_color\"/>"
      "      <input name=\"in2\" type=\"color3\" value=\"0.1, 0.2, 0.3\"/>"
      "    </add>"
      "    <output name=\"base_out\" type=\"color3\" nodename=\"base\"/>"
      "  </nodegraph>"
      "  <standard_surface name=\"StandardSurface\" type=\"surfaceshader\">"
      "    <input name=\"base_color\" type=\"color3\""
      "           nodegraph=\"NG_standard\" output=\"base_out\"/>"
      "    <input name=\"roughness\" type=\"float\" value=\"0.42\"/>"
      "    <input name=\"specular_ior\" type=\"float\" value=\"1.55\"/>"
      "    <input name=\"coat_ior\" type=\"float\" value=\"1.45\"/>"
      "    <input name=\"opacity\" type=\"float\" value=\"0.37\"/>"
      "  </standard_surface>"
      "  <surfacematerial name=\"StandardGraphMat\">"
      "    <input name=\"surfaceshader\" type=\"surfaceshader\""
      "           nodename=\"StandardSurface\"/>"
      "  </surfacematerial>"
      "</materialx>";
  tusdview::tydra::LightRtOpenPBRParams standardGraphEval;
  if (!tusdview::EvaluateMaterialXStringToLightRtOpenPBR(
          standardGraphXml, "StandardGraphMat", &standardGraphEval,
          &graphErr)) {
    std::fprintf(stderr, "MaterialX standard_surface eval failed: %s\n",
                 graphErr.c_str());
    return 1;
  }
  if (!Near(standardGraphEval.baseColor[0], 0.5f) ||
      !Near(standardGraphEval.baseColor[1], 0.6f) ||
      !Near(standardGraphEval.baseColor[2], 0.7f) ||
      !Near(standardGraphEval.specularRoughness, 0.42f) ||
      !Near(standardGraphEval.specularIor, 1.55f) ||
      !Near(standardGraphEval.coatIor, 1.45f) ||
      !Near(standardGraphEval.opacity, 0.37f)) {
    std::fprintf(stderr,
                 "MaterialX standard_surface graph values were not evaluated\n");
    return 1;
  }

  tusdview::DrawMaterialCPU ifaceGraphMat;
  ifaceGraphMat.name = "InterfaceGraph";
  ifaceGraphMat.hasOpenPBRSurface = true;
  ifaceGraphMat.materialXNodeGraphJson = R"json({
    "version": "1.39",
    "nodegraph": {
      "name": "NG_interface",
      "inputs": [
        {
          "name": "tint",
          "type": "color3",
          "value": [0.25, 0.5, 0.75]
        }
      ],
      "nodes": [
        {
          "name": "mul",
          "category": "multiply",
          "type": "color3",
          "inputs": [
            {"name": "in1", "type": "color3", "interfacename": "tint"},
            {"name": "in2", "type": "color3", "value": [2.0, 0.5, 0.25]}
          ]
        }
      ],
      "outputs": [
        {
          "name": "out",
          "type": "color3",
          "nodename": "mul"
        }
      ]
    },
    "connections": [
      {
        "input": "base_color",
        "nodegraph": "NG_interface",
        "output": "out"
      }
    ]
  })json";
  tusdview::BakeLightRtOpenPBR(&ifaceGraphMat);
  if (!ifaceGraphMat.hasLightRtOpenPBR ||
      !Near(ifaceGraphMat.lightRtOpenPBR.baseColor[0], 0.5f) ||
      !Near(ifaceGraphMat.lightRtOpenPBR.baseColor[1], 0.25f) ||
      !Near(ifaceGraphMat.lightRtOpenPBR.baseColor[2], 0.1875f)) {
    std::fprintf(stderr,
                 "MaterialX interface input graph was not evaluated\n");
    return 1;
  }

  tusdview::DrawMaterialCPU channelsGraphMat;
  channelsGraphMat.name = "ChannelsGraph";
  channelsGraphMat.hasOpenPBRSurface = true;
  channelsGraphMat.materialXNodeGraphJson = R"json({
    "version": "1.39",
    "nodegraph": {
      "name": "NG_channels",
      "nodes": [
        {
          "name": "packed",
          "category": "constant",
          "type": "color4",
          "inputs": [
            {"name": "value", "type": "color4", "value": [0.1, 0.2, 0.3, 0.4]}
          ]
        },
        {
          "name": "bgr",
          "category": "swizzle",
          "type": "color3",
          "inputs": [
            {"name": "in", "type": "color4", "nodename": "packed"},
            {"name": "channels", "type": "string", "value": "bgr"}
          ]
        }
      ],
      "outputs": [
        {
          "name": "base_out",
          "type": "color3",
          "nodename": "bgr"
        },
        {
          "name": "packed_out",
          "type": "color4",
          "nodename": "packed"
        }
      ]
    },
    "connections": [
      {
        "input": "base_color",
        "nodegraph": "NG_channels",
        "output": "base_out"
      },
      {
        "input": "base_roughness",
        "nodegraph": "NG_channels",
        "output": "packed_out",
        "channels": "g"
      },
      {
        "input": "opacity",
        "nodegraph": "NG_channels",
        "output": "packed_out",
        "channels": "a"
      }
    ]
  })json";
  tusdview::BakeLightRtOpenPBR(&channelsGraphMat);
  if (!channelsGraphMat.hasLightRtOpenPBR ||
      !Near(channelsGraphMat.lightRtOpenPBR.baseColor[0], 0.3f) ||
      !Near(channelsGraphMat.lightRtOpenPBR.baseColor[1], 0.2f) ||
      !Near(channelsGraphMat.lightRtOpenPBR.baseColor[2], 0.1f) ||
      !Near(channelsGraphMat.lightRtOpenPBR.specularRoughness, 0.2f) ||
      !Near(channelsGraphMat.lightRtOpenPBR.opacity, 0.4f)) {
    std::fprintf(stderr,
                 "MaterialX channel selector graph was not evaluated\n");
    return 1;
  }

  tusdview::DrawMaterialCPU imageGraphMat;
  imageGraphMat.name = "ImageGraph";
  imageGraphMat.hasOpenPBRSurface = true;
  imageGraphMat.params.push_back(Vec3Param("base_color", 0.2f, 0.4f, 0.6f));
  imageGraphMat.params.push_back(FloatParam("base_metalness", 0.25f));
  imageGraphMat.params.push_back(FloatParam("base_roughness", 0.5f));
  imageGraphMat.materialXNodeGraphJson = R"json({
    "version": "1.39",
    "nodegraph": {
      "name": "NG_image_graph",
      "nodes": [
        {
          "name": "base_img",
          "category": "image",
          "type": "color3",
          "inputs": [
            {"name": "file", "type": "filename", "value": "missing_base.png"},
            {"name": "default", "type": "color3", "value": [0.9, 0.1, 0.1]}
          ]
        },
        {
          "name": "metal_mul",
          "category": "multiply",
          "type": "float",
          "inputs": [
            {"name": "in1", "type": "float", "value": 0.6},
            {"name": "in2", "type": "float", "value": 0.7}
          ]
        }
      ],
      "outputs": [
        {
          "name": "base_color_output",
          "type": "color3",
          "nodename": "base_img",
          "output": "out"
        },
        {
          "name": "metal_output",
          "type": "float",
          "nodename": "metal_mul",
          "output": "out"
        }
      ]
    },
    "connections": [
      {
        "input": "base_color",
        "nodegraph": "NG_image_graph",
        "output": "base_color_output"
      },
      {
        "input": "base_metalness",
        "nodegraph": "NG_image_graph",
        "output": "metal_output"
      }
    ]
  })json";
  tusdview::BakeLightRtOpenPBR(&imageGraphMat);
  if (!imageGraphMat.hasLightRtOpenPBR ||
      !imageGraphMat.lightRtOpenPBR.hasTextureInputs) {
    std::fprintf(stderr, "MaterialX image graph was not marked texture-dependent\n");
    return 1;
  }
  if (!Near(imageGraphMat.lightRtOpenPBR.baseColor[0], 0.2f) ||
      !Near(imageGraphMat.lightRtOpenPBR.baseColor[1], 0.4f) ||
      !Near(imageGraphMat.lightRtOpenPBR.baseColor[2], 0.6f)) {
    std::fprintf(stderr,
                 "MaterialX image graph default was incorrectly constant-baked\n");
    return 1;
  }
  if (!Near(imageGraphMat.lightRtOpenPBR.metalness, 0.42f)) {
    std::fprintf(stderr,
                 "MaterialX mixed graph constant lane was not baked\n");
    return 1;
  }

  tusdview::DrawMaterialCPU fixedIdGraphMat;
  fixedIdGraphMat.name = "FixedIdGraph";
  fixedIdGraphMat.hasOpenPBRSurface = true;
  fixedIdGraphMat.materialXNodeGraphJson = R"json({
    "version": "1.39",
    "nodegraph": {
      "name": "NG_fixed_id_graph",
      "nodes": [
        {
          "name": "tint_mul",
          "category": "MaterialXMultiply",
          "type": "ND_multiply_color3",
          "inputs": [
            {"name": "in1", "type": "color3", "value": [0.2, 0.4, 0.6]},
            {"name": "in2", "type": "color3", "value": [2.0, 0.5, 0.25]}
          ]
        }
      ],
      "outputs": [
        {
          "name": "base_color_output",
          "type": "color3",
          "nodename": "tint_mul",
          "output": "out"
        }
      ]
    },
    "connections": [
      {
        "input": "base_color",
        "nodegraph": "NG_fixed_id_graph",
        "output": "base_color_output"
      }
    ]
  })json";
  tusdview::BakeLightRtOpenPBR(&fixedIdGraphMat);
  if (!fixedIdGraphMat.hasLightRtOpenPBR ||
      !Near(fixedIdGraphMat.lightRtOpenPBR.baseColor[0], 0.4f) ||
      !Near(fixedIdGraphMat.lightRtOpenPBR.baseColor[1], 0.2f) ||
      !Near(fixedIdGraphMat.lightRtOpenPBR.baseColor[2], 0.15f)) {
    std::fprintf(stderr,
                 "MaterialX fixed info:id category graph was not normalized\n");
    return 1;
  }

  tusdview::DrawMaterialCPU normalGraphMat;
  normalGraphMat.name = "NormalGraph";
  normalGraphMat.hasOpenPBRSurface = true;
  normalGraphMat.materialXNodeGraphJson = R"json({
    "version": "1.39",
    "nodegraph": {
      "name": "NG_normal_graph",
      "nodes": [
        {
          "name": "normal_img",
          "category": "image",
          "type": "vector3",
          "inputs": [
            {"name": "file", "type": "filename", "value": "missing_normal.png"},
            {"name": "default", "type": "vector3", "value": [0.5, 0.5, 1.0]}
          ]
        },
        {
          "name": "normal_map",
          "category": "normalmap",
          "type": "vector3",
          "inputs": [
            {
              "name": "in",
              "type": "vector3",
              "nodename": "normal_img",
              "output": "out"
            }
          ]
        }
      ],
      "outputs": [
        {
          "name": "normal_output",
          "type": "vector3",
          "nodename": "normal_map",
          "output": "out"
        }
      ]
    },
    "connections": [
      {
        "input": "normal",
        "nodegraph": "NG_normal_graph",
        "output": "normal_output"
      }
    ]
  })json";
  tusdview::BakeLightRtOpenPBR(&normalGraphMat);
  if (!normalGraphMat.hasLightRtOpenPBR ||
      !normalGraphMat.lightRtOpenPBR.hasTextureInputs ||
      !normalGraphMat.lightRtOpenPBR.hasNormalInput) {
    std::fprintf(stderr,
                 "MaterialX normalmap graph was not marked as normal texture\n");
    return 1;
  }
  if (!Near(normalGraphMat.lightRtOpenPBR.normal[0], 0.0f) ||
      !Near(normalGraphMat.lightRtOpenPBR.normal[1], 0.0f) ||
      !Near(normalGraphMat.lightRtOpenPBR.normal[2], 1.0f)) {
    std::fprintf(stderr,
                 "MaterialX normalmap default was incorrectly constant-baked\n");
    return 1;
  }

  // UsdPreviewSurface keeps its authored fallback parameters in `params` even
  // when the corresponding input is texture-driven. The canonical LightRT
  // pack must use neutral factors for those lanes, matching the raster fields;
  // Vulkan RT and CUDA consume this pack directly.
  tusdview::DrawMaterialCPU previewTextureMat;
  previewTextureMat.hasUsdPreviewSurface = true;
  previewTextureMat.baseColorTex = 0;
  previewTextureMat.metallicTex = 1;
  previewTextureMat.roughnessTex = 2;
  previewTextureMat.emissiveTex = 3;
  previewTextureMat.opacityTex = 4;
  previewTextureMat.specularColorTex = 5;
  previewTextureMat.coatWeightTex = 6;
  previewTextureMat.coatColorTex = 7;
  previewTextureMat.coatRoughnessTex = 8;
  previewTextureMat.params.push_back(
      Vec3Param("diffuseColor", 0.2f, 0.3f, 0.4f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(FloatParam("metallic", 0.0f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(FloatParam("roughness", 0.2f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(
      Vec3Param("emissiveColor", 0.0f, 0.0f, 0.0f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(FloatParam("opacity", 0.1f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(
      Vec3Param("specularColor", 0.04f, 0.04f, 0.04f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(FloatParam("clearcoat", 0.0f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  previewTextureMat.params.push_back(FloatParam("clearcoatRoughness", 0.1f));
  previewTextureMat.params.back().shader = "UsdPreviewSurface";
  tusdview::BakeRealtimePbrMaterial(&previewTextureMat);
  const auto& previewPbr = previewTextureMat.lightRtOpenPBR;
  if (!Near(previewPbr.baseColor[0], 1.0f) ||
      !Near(previewPbr.metalness, 1.0f) ||
      !Near(previewPbr.specularRoughness, 1.0f) ||
      !Near(previewPbr.emissionColor[0], 1.0f) ||
      !Near(previewPbr.emission, 1.0f) || !Near(previewPbr.opacity, 1.0f) ||
      !Near(previewPbr.specularColor[0], 1.0f) ||
      !Near(previewPbr.coatWeight, 1.0f) ||
      !Near(previewPbr.coatColor[0], 1.0f) ||
      !Near(previewPbr.coatRoughness, 1.0f)) {
    std::fprintf(stderr,
                 "UsdPreviewSurface texture factors were not neutralized in LightRT\n");
    return 1;
  }

  tusdview::DrawMaterialCPU standardRoughnessTextureMat;
  standardRoughnessTextureMat.hasOpenPBRSurface = true;
  standardRoughnessTextureMat.roughnessTex = 0;
  auto baseRoughness = FloatParam("base_roughness", 0.0f);
  baseRoughness.shader = "OpenPBRSurface";
  standardRoughnessTextureMat.params.push_back(baseRoughness);
  auto specularRoughness = FloatParam("specular_roughness", 0.5f);
  specularRoughness.shader = "OpenPBRSurface";
  specularRoughness.texture = 0;
  standardRoughnessTextureMat.params.push_back(specularRoughness);
  tusdview::BakeRealtimePbrMaterial(&standardRoughnessTextureMat);
  if (!Near(standardRoughnessTextureMat.lightRtOpenPBR.specularRoughness,
            1.0f)) {
    std::fprintf(stderr,
                 "Textured Standard Surface roughness alias was not neutralized\n");
    return 1;
  }

  // The shared RT texture table must retain complete RGBA mip chains as
  // consecutive descriptors so Vulkan, CUDA, and HIP use the same trilinear
  // sampling ABI.
  tusdview::DrawTextureCPU mipTexture;
  mipTexture.image.width = 4;
  mipTexture.image.height = 4;
  mipTexture.image.channels = 4;
  mipTexture.image.data.assign(4 * 4 * 4, 255);
  light3d::Image mip2;
  mip2.width = 2;
  mip2.height = 2;
  mip2.channels = 4;
  mip2.data.assign(2 * 2 * 4, 128);
  light3d::Image mip1;
  mip1.width = 1;
  mip1.height = 1;
  mip1.channels = 4;
  mip1.data.assign(4, 64);
  mipTexture.mipImages = {mip2, mip1};
  tusdview::DrawMaterialCPU mipMaterial;
  mipMaterial.baseColorTex = 0;
  tusdview::HostTextureTable mipTable;
  tusdview::BuildHostTextureTable({mipTexture}, {mipMaterial}, &mipTable);
  if (mipTable.textures.size() != 3 || mipTable.texels.size() != 84 ||
      mipTable.sourceToTable.size() != 1 || mipTable.sourceToTable[0] != 0 ||
      mipTable.matTex.empty() || mipTable.matTex[0] != 0 ||
      mipTable.textures[0].mipCount != 3 ||
      mipTable.textures[0].firstMip != 1 ||
      mipTable.textures[1].mipCount != 2 ||
      mipTable.textures[1].firstMip != 2 ||
      mipTable.textures[2].mipCount != 1 ||
      mipTable.textures[2].firstMip != -1) {
    std::fprintf(stderr, "shared RT mip-chain packing is incorrect\n");
    return 1;
  }
  tusdview::DrawTextureCPU udimTexture;
  udimTexture.isUdim = true;
  tusdview::DrawUdimTileCPU tile;
  tile.udim = 1001;
  tile.image = mipTexture.image;
  tile.mipImages = mipTexture.mipImages;
  udimTexture.udimTiles.push_back(std::move(tile));
  // Preserve a sparse UDIM address: tile 1002 is intentionally absent while
  // 1003 retains a distinct mip chain. A dense remap would make shader lookup
  // sample the wrong tile for UV (2, 0).
  tusdview::DrawUdimTileCPU sparseTile;
  sparseTile.udim = 1003;
  sparseTile.image = mipTexture.image;
  sparseTile.mipImages = mipTexture.mipImages;
  udimTexture.udimTiles.push_back(std::move(sparseTile));
  tusdview::HostTextureTable udimTable;
  tusdview::BuildHostTextureTable({udimTexture}, {mipMaterial}, &udimTable);
  if (udimTable.textures.size() != 7 ||
      udimTable.textures[0].isUdim != 1 ||
      udimTable.textures[0].udimLayer[0] != 1 ||
      udimTable.textures[0].udimLayer[1] != -1 ||
      udimTable.textures[0].udimLayer[2] != 4 ||
      udimTable.textures[1].mipCount != 3 ||
      udimTable.textures[1].firstMip != 2 ||
      udimTable.textures[4].mipCount != 3 ||
      udimTable.textures[4].firstMip != 5) {
    std::fprintf(stderr, "shared RT UDIM mip-chain packing is incorrect\n");
    return 1;
  }

  return 0;
}
