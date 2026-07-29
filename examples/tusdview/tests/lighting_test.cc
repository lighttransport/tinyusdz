// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"
#include "raster_lighting.hh"
#include "rt_scene_build.hh"
#include "texture_tools.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "tydra/render-data.hh"

namespace {

namespace tydra = tinyusdz::tydra;

bool Near(float a, float b, float eps = 1.0e-5f) {
  return std::fabs(a - b) <= eps;
}

void Identity(float m[16]) {
  for (int i = 0; i < 16; ++i) m[i] = 0.0f;
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

int AddImage(tydra::RenderScene* scene) {
  tydra::BufferData buf;
  buf.data = {
      255, 128, 64, 255,
      32, 64, 128, 255,
  };
  const int bufferId = static_cast<int>(scene->buffers.size());
  scene->buffers.push_back(std::move(buf));

  tydra::TextureImage img;
  img.buffer_id = bufferId;
  img.decoded = true;
  img.width = 2;
  img.height = 1;
  img.channels = 4;
  img.texelComponentType = tydra::ComponentType::UInt8;
  img.colorSpace = tydra::ColorSpace::Lin_sRGB;
  const int imageId = static_cast<int>(scene->images.size());
  scene->images.push_back(std::move(img));
  return imageId;
}

}  // namespace

int main() {
  tydra::RenderScene scene;

  tydra::RenderLight rect;
  rect.name = "rect_key";
  rect.type = tydra::RenderLight::Type::Rect;
  rect.color = {0.25f, 0.5f, 1.0f};
  rect.intensity = 4.0f;
  rect.exposure = 1.0f;
  rect.normalize = true;
  rect.width = 2.0f;
  rect.height = 4.0f;
  rect.position = {0.0f, 4.0f, 0.0f};
  rect.shapingConeAngle = 30.0f;
  rect.shapingFocusTint = {0.1f, 0.2f, 0.3f};
  rect.shapingIesFile = "profiles/key.ies";
  rect.shapingIesAngleScale = 1.25f;
  rect.shapingIesNormalize = true;
  rect.shadowColor = {0.05f, 0.06f, 0.07f};
  rect.shadowDistance = 12.0f;
  rect.shadowFalloff = 3.0f;
  rect.shadowFalloffGamma = 2.0f;
  rect.light_links_all = false;
  rect.light_link_mesh_indices = {3, 5};
  tydra::SpectralEmission rectSpd;
  rectSpd.samples = {
      tydra::vec2{450.0f, 0.25f},
      tydra::vec2{550.0f, 1.0f},
  };
  rect.spd_emission = rectSpd;
  scene.lights.push_back(rect);

  tydra::RenderLight dome;
  dome.name = "dome_env";
  dome.type = tydra::RenderLight::Type::Dome;
  dome.transform = tydra::mat4::identity();
  dome.envmap_texture_id = AddImage(&scene);
  dome.domeTextureFormat = tydra::RenderLight::DomeTextureFormat::Latlong;
  scene.lights.push_back(dome);

  tusdview::DrawScene draw;
  tusdview::BuildDrawScene(scene, &draw);

  if (draw.lights.size() != 2) {
    std::fprintf(stderr, "expected two lights, got %zu\n", draw.lights.size());
    return 1;
  }

  const tusdview::DrawLightCPU& key = draw.lights[0];
  if (!Near(key.effectiveIntensity, 8.0f) ||
      !Near(key.effectiveColor[0], 2.0f) ||
      !Near(key.effectiveColor[1], 4.0f) ||
      !Near(key.effectiveColor[2], 8.0f) ||
      !Near(key.area, 8.0f) || !Near(key.invArea, 0.125f) ||
      !Near(key.normalizedColor[0], 0.25f) ||
      !Near(key.normalizedColor[1], 0.5f) ||
      !Near(key.normalizedColor[2], 1.0f)) {
    std::fprintf(stderr, "derived rect light values are wrong\n");
    return 1;
  }

  // Raster light packing is shared by GL/Vulkan. Dome lights are excluded from
  // direct evaluation, and authored collections become a compact per-mesh mask.
  const tusdview::RasterLightSet raster =
      tusdview::PackRasterLights(draw.lights, 7);
  if (raster.count != 1 || raster.truncated != 0 ||
      raster.meshMasks.size() != 7 || raster.meshMasks[3] != 1u ||
      raster.meshMasks[5] != 1u || raster.meshMasks[0] != 0u) {
    std::fprintf(stderr, "raster light packing/link mask mismatch\n");
    return 1;
  }
  if (!Near(raster.lights[0].positionType[0], key.position[0]) ||
      !Near(raster.lights[0].colorDiffuse[0], key.normalizedColor[0]) ||
      !Near(raster.lights[0].colorDiffuse[3], key.diffuse) ||
      !Near(raster.lights[0].specularShape[0], key.specular) ||
      raster.lights[0].specularShape[3] != 1.0f) {
    std::fprintf(stderr, "raster packed light fields mismatch\n");
    return 1;
  }

  // Packing is deterministic and bounded. DistantLight stores its authored
  // emission direction in DrawLightCPU, so the raster record must expose the
  // opposite surface-to-light vector used by the BRDF.
  std::vector<tusdview::DrawLightCPU> manyLights;
  for (int i = 0; i < tusdview::kMaxRasterLights + 2; ++i) {
    tusdview::DrawLightCPU light;
    light.type = tusdview::DrawLightCPU::Type::Distant;
    light.direction[0] = 0.0f;
    light.direction[1] = -1.0f;
    light.direction[2] = static_cast<float>(i);
    manyLights.push_back(light);
  }
  const tusdview::RasterLightSet bounded =
      tusdview::PackRasterLights(manyLights, 1);
  if (bounded.count != tusdview::kMaxRasterLights || bounded.truncated != 2 ||
      bounded.meshMasks.size() != 1 || bounded.meshMasks[0] != 0xffffu ||
      !Near(bounded.lights[0].directionAngle[0], 0.0f) ||
      !Near(bounded.lights[0].directionAngle[1], 1.0f) ||
      !Near(bounded.lights[1].directionAngle[2], -1.0f)) {
    std::fprintf(stderr, "raster light bound/direction mismatch\n");
    return 1;
  }
  if (bounded.shadowLightSlot != 0) {
    std::fprintf(stderr, "first enabled distant shadow selection mismatch\n");
    return 1;
  }
  std::vector<tusdview::DrawLightCPU> shadowFallback(3);
  shadowFallback[0].type = tusdview::DrawLightCPU::Type::Sphere;
  shadowFallback[0].hasShaping = true;
  shadowFallback[0].shadowEnable = false;
  shadowFallback[1].type = tusdview::DrawLightCPU::Type::Sphere;
  shadowFallback[1].hasShaping = true;
  shadowFallback[2].type = tusdview::DrawLightCPU::Type::Rect;
  const tusdview::RasterLightSet fallback =
      tusdview::PackRasterLights(shadowFallback, 0);
  if (fallback.shadowLightSlot != 1) {
    std::fprintf(stderr, "shaped-sphere shadow fallback selection mismatch\n");
    return 1;
  }
  std::vector<tusdview::DrawLightCPU> areaFallback(2);
  areaFallback[0].type = tusdview::DrawLightCPU::Type::Point;
  areaFallback[0].shadowEnable = false;
  areaFallback[1].type = tusdview::DrawLightCPU::Type::Rect;
  areaFallback[1].position[1] = 4.0f;
  areaFallback[1].direction[1] = -1.0f;
  const tusdview::RasterLightSet areaLights =
      tusdview::PackRasterLights(areaFallback, 0);
  if (areaLights.shadowLightSlot != 1) {
    std::fprintf(stderr, "one-sided area-light shadow fallback mismatch\n");
    return 1;
  }
  std::vector<tusdview::DrawLightCPU> linkedShadow(1);
  linkedShadow[0].type = tusdview::DrawLightCPU::Type::Distant;
  linkedShadow[0].shadowLinksAll = false;
  linkedShadow[0].shadowLinkMeshIndices = {1};
  const tusdview::RasterLightSet linked =
      tusdview::PackRasterLights(linkedShadow, 3);
  if (tusdview::RasterShadowIncludesMesh(linked, 0) ||
      !tusdview::RasterShadowIncludesMesh(linked, 1) ||
      tusdview::RasterShadowIncludesMesh(linked, 2)) {
    std::fprintf(stderr, "raster shadow-link mask mismatch\n");
    return 1;
  }
  linkedShadow[0].lightLinksAll = false;
  linkedShadow[0].lightLinkMeshIndices = {0, 2};
  linkedShadow.push_back(tusdview::DrawLightCPU{});
  linkedShadow[1].lightLinksAll = true;
  linkedShadow[1].shadowLinksAll = false;
  linkedShadow[1].shadowLinkMeshIndices = {2};
  if (tusdview::RtLightCollectionMaskForMesh(linkedShadow, 0, false) != 3u ||
      tusdview::RtLightCollectionMaskForMesh(linkedShadow, 1, false) != 2u ||
      tusdview::RtLightCollectionMaskForMesh(linkedShadow, 2, true) != 2u ||
      tusdview::RtLightCollectionMaskForMesh(linkedShadow, 1, true) != 1u) {
    std::fprintf(stderr, "RT light/shadow collection mask mismatch\n");
    return 1;
  }
  const float shadowMin[3] = {-2.0f, -1.0f, -3.0f};
  const float shadowExtent[3] = {4.0f, 2.0f, 6.0f};
  std::vector<tusdview::DrawLightCPU> pointShadow(1);
  pointShadow[0].type = tusdview::DrawLightCPU::Type::Point;
  pointShadow[0].position[1] = 4.0f;
  const tusdview::RasterLightSet pointLights =
      tusdview::PackRasterLights(pointShadow, 0);
  tusdview::RasterShadowCamera pointShadowCamera;
  if (pointLights.shadowLightSlot != 0 ||
      !tusdview::BuildRasterShadowCamera(pointLights, shadowMin, shadowExtent,
                                         true, &pointShadowCamera) ||
      pointShadowCamera.perspective) {
    std::fprintf(stderr, "point-light shadow approximation mismatch\n");
    return 1;
  }
  tusdview::RasterPointShadowCameras pointCubeGl;
  tusdview::RasterPointShadowCameras pointCubeVk;
  bool oppositeFacesDiffer = false;
  if (!tusdview::BuildRasterPointShadowCameras(pointLights, shadowMin,
                                                shadowExtent, false,
                                                &pointCubeGl) ||
      !tusdview::BuildRasterPointShadowCameras(pointLights, shadowMin,
                                                shadowExtent, true,
                                                &pointCubeVk) ||
      pointCubeGl.lightSlot != 0 || pointCubeVk.lightSlot != 0 ||
      !(pointCubeGl.farPlane > pointCubeGl.nearPlane) ||
      !(pointCubeVk.farPlane > pointCubeVk.nearPlane)) {
    std::fprintf(stderr, "point-light cube shadow camera mismatch\n");
    return 1;
  }
  for (int i = 0; i < 16; ++i) {
    if (!Near(pointCubeGl.viewProj[0].m[i], pointCubeGl.viewProj[1].m[i])) {
      oppositeFacesDiffer = true;
      break;
    }
  }
  if (!oppositeFacesDiffer) {
    std::fprintf(stderr, "opposite point-light cube faces are identical\n");
    return 1;
  }
  if (tusdview::BuildRasterPointShadowCameras(bounded, shadowMin,
                                               shadowExtent, true,
                                               &pointCubeVk)) {
    std::fprintf(stderr, "non-point light unexpectedly produced cube shadows\n");
    return 1;
  }
  tusdview::RasterShadowCamera glShadow;
  tusdview::RasterShadowCamera vkShadow;
  if (!tusdview::BuildRasterShadowCamera(bounded, shadowMin, shadowExtent,
                                         false, &glShadow) ||
      !tusdview::BuildRasterShadowCamera(bounded, shadowMin, shadowExtent,
                                         true, &vkShadow) ||
      glShadow.lightSlot != 0 || glShadow.perspective ||
      vkShadow.lightSlot != 0 || vkShadow.perspective ||
      !(glShadow.farPlane > glShadow.nearPlane)) {
    std::fprintf(stderr, "distant shadow camera fitting mismatch\n");
    return 1;
  }
  tusdview::RasterShadowCamera sphereShadow;
  if (!tusdview::BuildRasterShadowCamera(fallback, shadowMin, shadowExtent,
                                         false, &sphereShadow) ||
      sphereShadow.lightSlot != 1 || !sphereShadow.perspective ||
      !(sphereShadow.farPlane > sphereShadow.nearPlane)) {
    std::fprintf(stderr, "sphere shadow camera fitting mismatch\n");
    return 1;
  }
  tusdview::RasterShadowCamera areaShadow;
  if (!tusdview::BuildRasterShadowCamera(areaLights, shadowMin, shadowExtent,
                                         true, &areaShadow) ||
      areaShadow.lightSlot != 1 || areaShadow.perspective ||
      !(areaShadow.farPlane > areaShadow.nearPlane)) {
    std::fprintf(stderr, "area-light shadow camera fitting mismatch\n");
    return 1;
  }
  if (!key.hasShaping || key.shapingIesFile != "profiles/key.ies" ||
      !Near(key.shapingIesAngleScale, 1.25f) || !key.shapingIesNormalize ||
      key.lightLinksAll || !key.hasSpectralEmission ||
      key.lightLinkMeshIndices.size() != 2) {
    std::fprintf(stderr, "light shaping/linking metadata was not preserved\n");
    return 1;
  }
  if (!draw.hasPreviewLight ||
      !Near(draw.previewLightDir[0], 0.0f) ||
      !Near(draw.previewLightDir[1], 1.0f) ||
      !Near(draw.previewLightDir[2], 0.0f) ||
      !Near(draw.previewLightColor[0], 0.25f) ||
      !Near(draw.previewLightColor[1], 0.5f) ||
      !Near(draw.previewLightColor[2], 1.0f)) {
    std::fprintf(stderr, "preview key light was not derived from USD light\n");
    return 1;
  }

  const tusdview::DrawLightCPU& env = draw.lights[1];
  if (env.renderEnvmapImage != 0 || env.envmapTexture < 0 ||
      static_cast<size_t>(env.envmapTexture) >= draw.textures.size()) {
    std::fprintf(stderr, "dome envmap image was not decoded into DrawScene\n");
    return 1;
  }
  const tusdview::DrawTextureCPU& tex =
      draw.textures[static_cast<size_t>(env.envmapTexture)];
  if (tex.image.width != 2 || tex.image.height != 1 || tex.image.channels != 4 ||
      tex.image.data.size() != 8) {
    std::fprintf(stderr, "unexpected decoded dome texture shape\n");
    return 1;
  }

  // Split-sum IBL bake (textools builds only; default domeIbl quality = high).
  if (tusdview::TexToolsAvailable()) {
    const tusdview::DomeIblCPU& ibl = env.ibl;
    if (!ibl.valid) {
      std::fprintf(stderr, "dome IBL was not baked\n");
      return 1;
    }
    if (ibl.specFaceSize != 64 || ibl.specLevels.size() != 5) {
      std::fprintf(stderr, "unexpected IBL spec chain shape\n");
      return 1;
    }
    for (size_t l = 0; l < ibl.specLevels.size(); ++l) {
      const size_t fs = static_cast<size_t>(ibl.specFaceSize) >> l;
      if (ibl.specLevels[l].size() != 6 * fs * fs * 3) {
        std::fprintf(stderr, "IBL spec level %zu has wrong size\n", l);
        return 1;
      }
    }
    if (ibl.irrFaceSize != 32 ||
        ibl.irradiance.size() !=
            6u * static_cast<size_t>(ibl.irrFaceSize) * ibl.irrFaceSize * 3u) {
      std::fprintf(stderr, "IBL irradiance has wrong size\n");
      return 1;
    }
    float irrMax = 0.0f;
    for (float v : ibl.irradiance) {
      if (v < 0.0f) {
        std::fprintf(stderr, "IBL irradiance has negative texels\n");
        return 1;
      }
      irrMax = std::max(irrMax, v);
    }
    if (!(irrMax > 0.0f)) {
      std::fprintf(stderr, "IBL irradiance is all zero\n");
      return 1;
    }
    if (ibl.lutSize != 64 ||
        ibl.brdfLut.size() != 2u * static_cast<size_t>(ibl.lutSize) * ibl.lutSize) {
      std::fprintf(stderr, "IBL BRDF LUT has wrong size\n");
      return 1;
    }
    // LUT indexed [roughness][NdotV] row-major: (rough~0, NdotV~1) must be
    // near (scale=1, bias=0).
    const size_t corner = (0u * static_cast<size_t>(ibl.lutSize) +
                           static_cast<size_t>(ibl.lutSize) - 1u) * 2u;
    const float scale = ibl.brdfLut[corner + 0];
    const float bias = ibl.brdfLut[corner + 1];
    if (!(scale > 0.9f && scale <= 1.05f && bias >= 0.0f && bias < 0.1f)) {
      std::fprintf(stderr, "IBL BRDF LUT corner is wrong: %f %f\n",
                   static_cast<double>(scale), static_cast<double>(bias));
      return 1;
    }
  }

  tydra::RenderScene formatScene;
  tydra::RenderLight mirrored;
  mirrored.name = "dome_mirrored";
  mirrored.type = tydra::RenderLight::Type::Dome;
  mirrored.envmap_texture_id = AddImage(&formatScene);
  mirrored.domeTextureFormat = tydra::RenderLight::DomeTextureFormat::MirroredBall;
  formatScene.lights.push_back(mirrored);
  tydra::RenderLight angular;
  angular.name = "dome_angular";
  angular.type = tydra::RenderLight::Type::Dome;
  angular.envmap_texture_id = AddImage(&formatScene);
  angular.domeTextureFormat = tydra::RenderLight::DomeTextureFormat::Angular;
  formatScene.lights.push_back(angular);
  tusdview::TextureRuntimeOptions formatOpt;
  formatOpt.domeIbl = 0;
  tusdview::DrawScene formatDraw;
  tusdview::BuildDrawScene(formatScene, &formatDraw, nullptr, nullptr,
                           formatOpt);
  if (formatDraw.lights.size() != 2 || formatDraw.textures.size() != 2) {
    std::fprintf(stderr, "dome format scene did not preserve lights/textures\n");
    return 1;
  }
  if (formatDraw.lights[0].domeTextureFormat !=
          tusdview::DrawLightCPU::DomeTextureFormat::MirroredBall ||
      formatDraw.lights[1].domeTextureFormat !=
          tusdview::DrawLightCPU::DomeTextureFormat::Angular ||
      formatDraw.lights[0].envmapTexture < 0 ||
      formatDraw.lights[1].envmapTexture < 0) {
    std::fprintf(stderr, "MirroredBall/Angular dome formats were not preserved\n");
    return 1;
  }

  tusdview::DrawMaterialCPU mat;
  mat.name = "mat";
  draw.materials.push_back(mat);

  tusdview::DrawMeshCPU mesh;
  mesh.name = "tri";
  Identity(mesh.world);
  Identity(mesh.skinGeomBind);
  mesh.vertices.resize(3);
  mesh.vertices[0].px = 0.0f;
  mesh.vertices[0].py = 0.0f;
  mesh.vertices[0].pz = 0.0f;
  mesh.vertices[1].px = 1.0f;
  mesh.vertices[1].py = 0.0f;
  mesh.vertices[1].pz = 0.0f;
  mesh.vertices[2].px = 0.0f;
  mesh.vertices[2].py = 1.0f;
  mesh.vertices[2].pz = 0.0f;
  for (tusdview::DrawVertex& v : mesh.vertices) {
    v.nz = 1.0f;
  }
  mesh.indices = {0, 1, 2};
  tusdview::DrawSubmesh sub;
  sub.indexOffset = 0;
  sub.indexCount = 3;
  sub.materialId = 0;
  mesh.submeshes.push_back(sub);
  draw.meshes.push_back(mesh);

  tusdview::HostScene host;
  std::string err;
  if (!tusdview::BuildHostScene(draw, 0, 0, 0.0f, &host, &err)) {
    std::fprintf(stderr, "BuildHostScene failed: %s\n", err.c_str());
    return 1;
  }
  if (host.numLights != 2 ||
      host.lightParams.size() <
          2u * static_cast<size_t>(tusdview::kRtLightParamFloats)) {
    std::fprintf(stderr, "RT light buffer was not packed\n");
    return 1;
  }

  const float* lp = host.lightParams.data();
  const int keyFlags = static_cast<int>(lp[1] + 0.5f);
  if (!Near(lp[0], static_cast<float>(tusdview::DrawLightCPU::Type::Rect)) ||
      (keyFlags & (1 << 0)) == 0 || (keyFlags & (1 << 2)) == 0 ||
      (keyFlags & (1 << 3)) == 0 ||
      (keyFlags & (1 << 5)) != 0 ||
      !Near(lp[12], 0.25f) || !Near(lp[13], 0.5f) ||
      !Near(lp[14], 1.0f) || !Near(lp[15], 8.0f) ||
      !Near(lp[16], 2.0f) || !Near(lp[17], 4.0f) ||
      !Near(lp[18], 8.0f) || !Near(lp[19], 0.125f) ||
      !Near(lp[20], 2.0f) || !Near(lp[21], 4.0f) ||
      !Near(lp[23], 8.0f) || !Near(lp[24], 30.0f) ||
      !Near(lp[27], 1.25f) ||
      !Near(lp[32], 0.05f) || !Near(lp[33], 0.06f) ||
      !Near(lp[34], 0.07f) || !Near(lp[35], 12.0f) ||
      !Near(lp[36], 3.0f) || !Near(lp[37], 2.0f) ||
      !Near(lp[38], 1.0f)) {
    std::fprintf(stderr, "RT key light params were not packed from USD light\n");
    return 1;
  }

  const float* domeLp = lp + tusdview::kRtLightParamFloats;
  const int domeFlags = static_cast<int>(domeLp[1] + 0.5f);
  if (!Near(domeLp[0], static_cast<float>(tusdview::DrawLightCPU::Type::Dome)) ||
      (domeFlags & (1 << 4)) == 0 || !Near(domeLp[2], 0.0f)) {
    std::fprintf(stderr, "RT dome light envmap texture was not mapped\n");
    return 1;
  }

  // Rows 10-12: world->environment rotation (identity for the untransformed
  // dome). Rows 13-19: SH irradiance (bit 7 + non-zero coeffs when the IBL was
  // baked, i.e. when textools is available).
  if (!Near(domeLp[40], 1.0f) || !Near(domeLp[41], 0.0f) ||
      !Near(domeLp[42], 0.0f) || !Near(domeLp[45], 1.0f) ||
      !Near(domeLp[50], 1.0f)) {
    std::fprintf(stderr, "RT dome rotation rows are not identity\n");
    return 1;
  }
  if (tusdview::TexToolsAvailable()) {
    if ((domeFlags & (1 << 7)) == 0) {
      std::fprintf(stderr, "RT dome IBL-baked flag (bit 7) not set\n");
      return 1;
    }
    float shMag = 0.0f;
    for (int i = 0; i < 27; ++i) shMag += std::fabs(domeLp[52 + i]);
    if (!(shMag > 0.0f)) {
      std::fprintf(stderr, "RT dome SH irradiance rows are all zero\n");
      return 1;
    }
  }

  // Non-mesh carriers become camera-independent solid RT proxies: an
  // octahedron per point and a four-sided tube per tessellated curve segment.
  tusdview::DrawScene carrierScene;
  carrierScene.materials.push_back(tusdview::DrawMaterialCPU{});
  tusdview::DrawPointsCPU points;
  points.name = "point";
  points.points = {0.0f, 0.0f, 0.0f};
  points.widths = {2.0f};
  points.colors = {0.2f, 0.4f, 0.8f};
  points.opacities = {0.25f};
  points.materialId = 0;
  Identity(points.world);
  carrierScene.points.push_back(points);
  tusdview::DrawCurvesCPU curves;
  curves.name = "curve";
  curves.vertexCounts = {2};
  curves.points = {0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f};
  curves.widths = {0.5f};
  curves.colors = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  curves.opacities = {0.2f, 0.6f};
  curves.materialId = 0;
  Identity(curves.world);
  carrierScene.curves.push_back(curves);
  tusdview::HostScene carrierHost;
  if (!tusdview::BuildHostScene(carrierScene, 0, 0, 0.0f, &carrierHost,
                                &err) ||
      carrierHost.triCount != 48 || carrierHost.instCount != 2) {
    std::fprintf(stderr, "non-mesh RT proxy build failed: %s (%zu tris, %zu inst)\n",
                 err.c_str(), carrierHost.triCount, carrierHost.instCount);
    return 1;
  }
  bool foundPointOpacity = false;
  bool foundCurveOpacity = false;
  for (size_t i = 3; i < carrierHost.cols.size(); i += 4) {
    foundPointOpacity |= Near(carrierHost.cols[i], 0.25f);
    foundCurveOpacity |= Near(carrierHost.cols[i], 0.4f);
  }
  if (!foundPointOpacity || !foundCurveOpacity) {
    std::fprintf(stderr,
                 "non-mesh displayOpacity was not retained in RT proxies\n");
    return 1;
  }

  return 0;
}
