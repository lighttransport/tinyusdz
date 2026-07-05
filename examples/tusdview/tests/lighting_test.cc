// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"
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

  return 0;
}
