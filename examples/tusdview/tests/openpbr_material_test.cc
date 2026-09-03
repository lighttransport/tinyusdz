// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"
#include "lightrt_mtlx_bridge.hh"

#include <cmath>
#include <cstdio>
#include <vector>

#include "tydra/render-data-shader.hh"
#include "tydra/render-data.hh"
extern "C" {
#include "external/lightrt/mtlxrender/bsdf.h"
}

namespace {

namespace tydra = lightusd::tydra;

bool Near(float a, float b, float eps = 1.0e-6f) {
  return std::fabs(a - b) <= eps;
}

int AddImage(tydra::RenderScene* scene, tydra::ColorSpace colorSpace) {
  tydra::BufferData buf;
  buf.data = {255, 128, 64, 255};
  const int bufferId = static_cast<int>(scene->buffers.size());
  scene->buffers.push_back(std::move(buf));

  tydra::TextureImage img;
  img.buffer_id = bufferId;
  img.decoded = true;
  img.width = 1;
  img.height = 1;
  img.channels = 4;
  img.texelComponentType = tydra::ComponentType::UInt8;
  img.colorSpace = colorSpace;
  // BuildDrawScene preserves the authored USD color space per material
  // connection, rather than the decoded working color space alone.
  img.usdColorSpace = colorSpace;
  img.asset_identifier = "asset_" + std::to_string(scene->images.size()) + ".png";
  const int imageId = static_cast<int>(scene->images.size());
  scene->images.push_back(std::move(img));
  return imageId;
}

int AddTexture(tydra::RenderScene* scene, int imageId,
               tydra::UVTexture::Channel channel) {
  tydra::UVTexture tex;
  tex.prim_name = "tex" + std::to_string(scene->textures.size());
  tex.texture_image_id = imageId;
  tex.connectedOutputChannel = channel;
  tex.wrapS = tydra::UVTexture::WrapMode::REPEAT;
  tex.wrapT = tydra::UVTexture::WrapMode::MIRROR;
  tex.scale = {1.0f, 1.0f, 1.0f, 1.0f};
  tex.bias = {0.0f, 0.0f, 0.0f, 0.0f};
  const int texId = static_cast<int>(scene->textures.size());
  scene->textures.push_back(std::move(tex));
  return texId;
}

}  // namespace

int main() {
  // MaterialX's compatibility normalizer emits `noise3d` for MaterialXNoise
  // nodes. Preserve the authored z coordinate in the retained runtime IR
  // instead of silently degrading the graph to a 2D hash.
  {
    tusdview::DrawMaterialCPU noiseMaterial;
    noiseMaterial.materialXNodeGraphJson = R"json({
      "nodegraph": {"nodes": [
        {"name": "n", "category": "MaterialXNoise", "type": "float"}
      ]},
      "connections": []
    })json";
    std::string noiseError;
    if (!tusdview::CompileMaterialXGraphRuntime(&noiseMaterial, &noiseError) ||
        !noiseMaterial.materialXGraph.valid ||
        noiseMaterial.materialXGraph.nodes.empty() ||
        noiseMaterial.materialXGraph.nodes[0].op !=
            tusdview::MaterialXGraphOpCPU::Noise3D) {
      std::fprintf(stderr, "MaterialXNoise was not retained in runtime IR: %s\n",
                   noiseError.c_str());
      return 1;
    }
  }

  // Standard Surface/OpenPBR graphs used by OpenChessSet expose both body and
  // layer lobes as named graph outputs. These routes must survive into the
  // fixed-size runtime block for per-hit texture evaluation.
  {
    tusdview::DrawMaterialCPU subsurfaceMaterial;
    subsurfaceMaterial.materialXNodeGraphJson = R"json({
      "nodegraph": {
        "nodes": [{"name": "sss", "category": "constant", "type": "float", "inputs": [{"name": "value", "value": 0.5}]}],
        "outputs": [
          {"name": "subsurface_output", "nodename": "sss"},
          {"name": "base_color_output", "nodename": "sss"},
          {"name": "radius_output", "nodename": "sss"},
          {"name": "roughness_output", "nodename": "sss"},
          {"name": "specular_output", "nodename": "sss"},
          {"name": "transmission_output", "nodename": "sss"},
          {"name": "coat_output", "nodename": "sss"},
          {"name": "fuzz_output", "nodename": "sss"},
          {"name": "ior_output", "nodename": "sss"}
        ]
      },
      "connections": [
        {"input": "subsurface", "output": "subsurface_output"},
        {"input": "subsurface_color", "output": "base_color_output"},
        {"input": "subsurface_radius", "output": "radius_output"},
        {"input": "roughness", "output": "roughness_output"},
        {"input": "specular_color", "output": "specular_output"},
        {"input": "transmission", "output": "transmission_output"},
        {"input": "coat_color", "output": "coat_output"},
        {"input": "fuzz_weight", "output": "fuzz_output"},
        {"input": "specular_ior", "output": "ior_output"}
      ]
    })json";
    std::string subsurfaceError;
    if (!tusdview::CompileMaterialXGraphRuntime(&subsurfaceMaterial,
                                                &subsurfaceError) ||
        subsurfaceMaterial.materialXGraph.output[6] != 0 ||
        subsurfaceMaterial.materialXGraph.output[7] != 0 ||
        subsurfaceMaterial.materialXGraph.output[8] != 0 ||
        subsurfaceMaterial.materialXGraph.output[2] != 0 ||
        subsurfaceMaterial.materialXGraph.output[10] != 0 ||
        subsurfaceMaterial.materialXGraph.output[11] != 0 ||
        subsurfaceMaterial.materialXGraph.output[14] != 0 ||
        subsurfaceMaterial.materialXGraph.output[16] != 0 ||
        subsurfaceMaterial.materialXGraph.output[19] != 0) {
      std::fprintf(stderr,
                   "MaterialX subsurface routes were not retained: %s\n",
                   subsurfaceError.c_str());
      return 1;
    }
  }

  // Subsurface is a normalized-diffusion response, not merely a color tint:
  // zero radius stays Lambertian while a finite radius broadens grazing light.
  {
    OpenPBRParams p{};
    p.base_weight = 1.0f;
    p.base_color = v3_make(0.8f, 0.6f, 0.4f);
    p.specular_weight = 0.0f;
    p.specular_ior = 1.5f;
    p.specular_roughness = 0.5f;
    p.subsurface = 1.0f;
    p.subsurface_color = p.base_color;
    p.subsurface_radius = v3_splat(0.0f);
    p.subsurface_scale = 1.0f;
    p.sheen_color = v3_splat(1.0f);
    p.coat_color = v3_splat(1.0f);
    const v3 n = v3_make(0.0f, 0.0f, 1.0f);
    const v3 wo = v3_normalize(v3_make(0.8f, 0.0f, 0.6f));
    const v3 wi = v3_normalize(v3_make(-0.8f, 0.0f, 0.6f));
    float pdf0 = 0.0f, pdf1 = 0.0f;
    const v3 zero_radius = bsdf_eval(&p, n, wo, wi, &pdf0);
    p.subsurface_radius = v3_splat(2.0f);
    const v3 finite_radius = bsdf_eval(&p, n, wo, wi, &pdf1);
    if (!(pdf0 > 0.0f && pdf1 > 0.0f) ||
        std::fabs(finite_radius.x - zero_radius.x) < 1.0e-4f) {
      std::fprintf(stderr,
                   "subsurface radius did not alter normalized diffusion\n");
      return 1;
    }

    p.subsurface = 0.0f;
    p.sheen_weight = 0.0f;
    const v3 wi_fuzz = wo;
    const v3 plain = bsdf_eval(&p, n, wo, wi_fuzz, &pdf0);
    p.sheen_weight = 0.8f;
    p.sheen_roughness = 0.35f;
    const v3 fuzz = bsdf_eval(&p, n, wo, wi_fuzz, &pdf1);
    if (fuzz.x <= plain.x) {
      std::fprintf(stderr, "Charlie fuzz lobe did not add grazing response\n");
      return 1;
    }

    p.sheen_weight = 0.0f;
    p.specular_weight = 1.0f;
    p.thin_film_weight = 0.0f;
    const v3 uncoated = bsdf_eval(&p, n, wo, wi_fuzz, &pdf0);
    p.thin_film_weight = 1.0f;
    p.thin_film_thickness = 450.0f;
    p.thin_film_ior = 1.4f;
    const v3 film = bsdf_eval(&p, n, wo, wi_fuzz, &pdf1);
    const float film_delta = std::fabs(film.x - uncoated.x) +
                             std::fabs(film.y - uncoated.y) +
                             std::fabs(film.z - uncoated.z);
    if (film_delta < 1.0e-4f) {
      std::fprintf(stderr, "thin-film interference did not alter Fresnel\n");
      return 1;
    }

    p.transmission_color = v3_make(0.25f, 0.5f, 1.0f);
    p.transmission_depth = 2.0f;
    const VolumeMedium medium = transmission_medium(&p);
    if (!(medium.sigma_t.x > medium.sigma_t.y &&
          medium.sigma_t.y > medium.sigma_t.z && medium.sigma_t.z == 0.0f)) {
      std::fprintf(stderr, "transmission medium extinction is incorrect\n");
      return 1;
    }
  }
  tydra::RenderScene scene;

  const int baseImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture);
  const int mrImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int roughImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int emissiveImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture);
  const int normalImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int opacityImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int coatWeightImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int coatColorImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture);
  const int coatRoughImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int specularImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture);

  const int baseTex = AddTexture(&scene, baseImage, tydra::UVTexture::Channel::RGB);
  const int metalTex = AddTexture(&scene, mrImage, tydra::UVTexture::Channel::B);
  const int roughTex = AddTexture(&scene, roughImage, tydra::UVTexture::Channel::G);
  const int emissiveTex =
      AddTexture(&scene, emissiveImage, tydra::UVTexture::Channel::RGB);
  const int normalTex = AddTexture(&scene, normalImage, tydra::UVTexture::Channel::RGB);
  const int opacityTex = AddTexture(&scene, opacityImage, tydra::UVTexture::Channel::A);
  const int coatWeightTex =
      AddTexture(&scene, coatWeightImage, tydra::UVTexture::Channel::G);
  const int coatColorTex =
      AddTexture(&scene, coatColorImage, tydra::UVTexture::Channel::RGB);
  const int coatRoughTex =
      AddTexture(&scene, coatRoughImage, tydra::UVTexture::Channel::B);
  const int specularTex =
      AddTexture(&scene, specularImage, tydra::UVTexture::Channel::RGB);
  // A second UVTexture pointing at the metallic image must deduplicate to the
  // same DrawScene texture slot. This catches adapters that leave a material
  // sample carrying the source RenderScene index instead of the mapped id.
  const int sharedMrTex =
      AddTexture(&scene, mrImage, tydra::UVTexture::Channel::RGB);

  scene.textures[static_cast<size_t>(metalTex)].scale[2] = 0.25f;
  scene.textures[static_cast<size_t>(metalTex)].bias[2] = 0.10f;
  scene.textures[static_cast<size_t>(roughTex)].scale[1] = 0.50f;
  scene.textures[static_cast<size_t>(roughTex)].bias[1] = 0.20f;
  scene.textures[static_cast<size_t>(normalTex)].has_transform2d = true;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[0][0] = 2.0f;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[1][1] = 3.0f;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[2][0] = 0.25f;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[2][1] = 0.5f;
  scene.textures[static_cast<size_t>(sharedMrTex)].has_transform2d = true;
  scene.textures[static_cast<size_t>(sharedMrTex)].transform =
      scene.textures[static_cast<size_t>(normalTex)].transform;
  scene.textures[static_cast<size_t>(opacityTex)].scale[3] = 0.75f;
  scene.textures[static_cast<size_t>(opacityTex)].bias[3] = 0.05f;
  scene.textures[static_cast<size_t>(coatWeightTex)].scale[1] = 0.65f;
  scene.textures[static_cast<size_t>(coatWeightTex)].bias[1] = 0.15f;
  scene.textures[static_cast<size_t>(coatRoughTex)].scale[2] = 0.55f;
  scene.textures[static_cast<size_t>(coatRoughTex)].bias[2] = 0.25f;

  tydra::OpenPBRSurfaceShader shader;
  shader.base_color.value = {0.2f, 0.4f, 0.6f};
  shader.base_color.texture_id = baseTex;
  shader.base_metalness.value = 0.5f;
  shader.base_metalness.texture_id = metalTex;
  shader.base_roughness.value = 0.35f;
  shader.base_roughness.texture_id = roughTex;
  shader.normal.texture_id = normalTex;
  shader.coat_normal.texture_id = sharedMrTex;
  shader.coat_weight.value = 0.6f;
  shader.coat_weight.texture_id = coatWeightTex;
  shader.coat_color.value = {0.7f, 0.8f, 0.9f};
  shader.coat_color.texture_id = coatColorTex;
  shader.coat_roughness.value = 0.2f;
  shader.coat_roughness.texture_id = coatRoughTex;
  shader.specular_color.texture_id = specularTex;
  shader.coat_ior.value = 1.4f;
  shader.emission_luminance.value = 3.0f;
  shader.emission_color.value = {0.1f, 0.2f, 0.3f};
  shader.emission_color.texture_id = emissiveTex;
  shader.opacity.value = 0.7f;
  shader.opacity.texture_id = opacityTex;
  shader.transmission_weight.value = 0.2f;
  shader.subsurface_weight.value = 0.3f;
  shader.sheen_weight.value = 0.4f;
  shader.thin_film_weight.value = 0.5f;
  shader.specular_anisotropy.value = 0.6f;
  shader.transmission_dispersion.value = 0.7f;

  tydra::RenderMaterial material;
  material.name = "openpbr_textured";
  material.abs_path = "/World/Looks/OpenPBR";
  material.openPBRShader = shader;
  material.computeMaterialTag();
  scene.materials.push_back(std::move(material));

  tydra::OpenPBRSurfaceShader graphShader;
  graphShader.base_color.value = {0.05f, 0.05f, 0.05f};
  graphShader.base_metalness.value = 0.45f;
  graphShader.base_roughness.value = 0.25f;
  graphShader.emission_luminance.value = 1.5f;
  graphShader.emission_color.value = {0.2f, 0.4f, 0.8f};
  graphShader.opacity.value = 0.8f;
  graphShader.nodeGraphJson = R"json({
    "version": "1.39",
    "nodegraph": {
      "name": "NG_graph_openpbr",
      "nodes": [
        {
          "name": "mul_base",
          "category": "multiply",
          "type": "color3",
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
          "nodename": "mul_base",
          "output": "out"
        }
      ]
    },
    "connections": [
      {
        "input": "base_color",
        "nodegraph": "NG_graph_openpbr",
        "output": "base_color_output"
      }
    ]
  })json";

  tydra::RenderMaterial graphMaterial;
  graphMaterial.name = "openpbr_graph";
  graphMaterial.openPBRShader = graphShader;
  graphMaterial.computeMaterialTag();
  scene.materials.push_back(std::move(graphMaterial));

  tusdview::DrawScene draw;
  tusdview::BuildDrawScene(scene, &draw);
  if (draw.materials.size() != 2) {
    std::fprintf(stderr, "expected two materials, got %zu\n",
                 draw.materials.size());
    return 1;
  }
  if (draw.textures.size() != 10) {
    std::fprintf(stderr, "expected ten independent/deduplicated textures, got %zu\n",
                 draw.textures.size());
    return 1;
  }
  if (draw.textures[0].assetIdentifier != "asset_0.png" ||
      draw.textures[0].renderImageId != baseImage ||
      draw.textures[4].assetIdentifier != "asset_4.png" ||
      draw.textures[4].renderImageId != normalImage ||
      draw.textures[9].assetIdentifier != "asset_9.png" ||
      draw.textures[9].renderImageId != specularImage) {
    std::fprintf(stderr, "texture source metadata was not preserved\n");
    return 1;
  }

  const tusdview::DrawMaterialCPU& mat = draw.materials[0];
  if (mat.baseColorTex < 0 || mat.metallicTex < 0 || mat.roughnessTex < 0 || mat.normalTex < 0 ||
      mat.coatNormalTex < 0 || mat.emissiveTex < 0 || mat.opacityTex < 0 ||
      mat.coatWeightTex < 0 || mat.coatColorTex < 0 ||
      mat.coatRoughnessTex < 0 || mat.specularColorTex < 0) {
    std::fprintf(stderr, "OpenPBR texture slots were not populated\n");
    return 1;
  }
  if (mat.baseColorSample.tex != mat.baseColorTex ||
      mat.metallicSample.tex != mat.metallicTex ||
      mat.roughnessSample.tex != mat.roughnessTex ||
      mat.normalSample.tex != mat.normalTex ||
      mat.coatNormalSample.tex != mat.coatNormalTex ||
      mat.emissiveSample.tex != mat.emissiveTex ||
      mat.opacitySample.tex != mat.opacityTex ||
      mat.coatWeightSample.tex != mat.coatWeightTex ||
      mat.coatColorSample.tex != mat.coatColorTex ||
      mat.coatRoughnessSample.tex != mat.coatRoughnessTex ||
      mat.specularColorSample.tex != mat.specularColorTex) {
    std::fprintf(stderr, "OpenPBR texture descriptors are not self-contained\n");
    return 1;
  }
  // Every semantic slot must retain its own sampling intent after image
  // deduplication. In particular, scalar/normal inputs are Raw even when a
  // color input is sRGB, and the per-connection wrap mode belongs on the
  // descriptor rather than only on DrawScene::textures.
  const auto hasSamplingIntent = [](const tusdview::DrawTexSampleCPU& sample,
                                    tusdview::DrawColorSpace colorSpace) {
    return sample.wrapS == tusdview::WrapMode::Repeat &&
           sample.wrapT == tusdview::WrapMode::Mirror &&
           sample.colorSpace == colorSpace;
  };
  if (!hasSamplingIntent(mat.baseColorSample, tusdview::DrawColorSpace::sRGB) ||
      !hasSamplingIntent(mat.metallicSample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.roughnessSample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.normalSample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.coatNormalSample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.emissiveSample, tusdview::DrawColorSpace::sRGB) ||
      !hasSamplingIntent(mat.opacitySample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.coatWeightSample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.coatColorSample, tusdview::DrawColorSpace::sRGB) ||
      !hasSamplingIntent(mat.coatRoughnessSample, tusdview::DrawColorSpace::Raw) ||
      !hasSamplingIntent(mat.specularColorSample, tusdview::DrawColorSpace::sRGB)) {
    std::fprintf(stderr,
                 "OpenPBR texture descriptor wrap/color-space intent was lost\n");
    return 1;
  }
  if (mat.metallicTex != mat.baseColorTex + 1) {
    std::fprintf(stderr, "unexpected metallic texture order\n");
    return 1;
  }
  if (mat.roughnessTex != mat.baseColorTex + 2 ||
      mat.roughnessTex == mat.metallicTex) {
    std::fprintf(stderr, "independent roughness image collapsed into metallic\n");
    return 1;
  }
  if (mat.metallicChannel != 2 || mat.roughnessChannel != 1 ||
      !Near(mat.metallicTexScale, 0.25f) ||
      !Near(mat.metallicTexBias, 0.10f) ||
      !Near(mat.roughnessTexScale, 0.50f) ||
      !Near(mat.roughnessTexBias, 0.20f)) {
    std::fprintf(stderr, "OpenPBR packed-channel selectors were not preserved\n");
    return 1;
  }
  if (!Near(mat.baseColor[0], 1.0f) || !Near(mat.metallic, 1.0f) ||
      !Near(mat.roughness, 1.0f) || !Near(mat.emissive[0], 3.0f) ||
      !Near(mat.alpha, 1.0f)) {
    std::fprintf(stderr, "OpenPBR textured neutral factors are wrong\n");
    return 1;
  }
  if (!Near(mat.normalSample.scale[0], 2.0f) ||
      !Near(mat.normalSample.bias[0], -1.0f)) {
    std::fprintf(stderr, "OpenPBR normal texture unpack defaults are wrong\n");
    return 1;
  }
  if (!Near(mat.coatWeight, 1.0f) || !Near(mat.coatColor[0], 1.0f) ||
      !Near(mat.coatColor[1], 1.0f) || !Near(mat.coatColor[2], 1.0f) ||
      !Near(mat.coatRoughness, 1.0f) || !Near(mat.coatIor, 1.4f)) {
    std::fprintf(stderr, "OpenPBR textured coat factors were not neutralized\n");
    return 1;
  }
  if (mat.opacityChannel != 3 || !Near(mat.opacityTexScale, 0.75f) ||
      !Near(mat.opacityTexBias, 0.05f) ||
      !Near(mat.coatWeightSample.scale[1], 0.65f) ||
      !Near(mat.coatWeightSample.bias[1], 0.15f) ||
      !Near(mat.coatRoughnessSample.scale[2], 0.55f) ||
      !Near(mat.coatRoughnessSample.bias[2], 0.25f)) {
    std::fprintf(stderr, "OpenPBR scalar texture descriptors were not preserved\n");
    return 1;
  }
  if (!Near(mat.normalSample.uv.m00, 2.0f) ||
      !Near(mat.normalSample.uv.m11, 3.0f) ||
      !Near(mat.normalSample.uv.tx, 0.25f) ||
      !Near(mat.normalSample.uv.ty, 0.5f)) {
    std::fprintf(stderr, "OpenPBR normal texture UV transform was not preserved\n");
    return 1;
  }
  bool normalParamCarriesUv = false;
  for (const tusdview::DrawMaterialParamCPU& param : mat.params) {
    if (param.shader == "OpenPBRSurface" && param.name == "normal") {
      normalParamCarriesUv =
          Near(param.sample.uv.m00, 2.0f) &&
          Near(param.sample.uv.m11, 3.0f) &&
          Near(param.sample.uv.tx, 0.25f) &&
          Near(param.sample.uv.ty, 0.5f);
    }
  }
  if (!normalParamCarriesUv) {
    std::fprintf(stderr, "OpenPBR normal parameter UV transform was not preserved\n");
    return 1;
  }
  if (mat.coatNormalTex != mat.metallicTex ||
      !Near(mat.coatNormalSample.scale[0], 2.0f) ||
      !Near(mat.coatNormalSample.bias[0], -1.0f) ||
      !Near(mat.coatNormalSample.uv.m00, 2.0f) ||
      !Near(mat.coatNormalSample.uv.m11, 3.0f) ||
      !Near(mat.coatNormalSample.uv.tx, 0.25f) ||
      !Near(mat.coatNormalSample.uv.ty, 0.5f)) {
    std::fprintf(stderr, "OpenPBR coat normal texture metadata was not preserved\n");
    return 1;
  }
  bool coatNormalParamCarriesUv = false;
  for (const tusdview::DrawMaterialParamCPU& param : mat.params) {
    if (param.shader == "OpenPBRSurface" && param.name == "coat_normal") {
      coatNormalParamCarriesUv =
          Near(param.sample.uv.m00, 2.0f) &&
          Near(param.sample.uv.m11, 3.0f) &&
          Near(param.sample.uv.tx, 0.25f) &&
          Near(param.sample.uv.ty, 0.5f);
    }
  }
  if (!coatNormalParamCarriesUv) {
    std::fprintf(stderr, "OpenPBR coat normal parameter UV transform was not preserved\n");
    return 1;
  }
  if (!mat.hasLightRtOpenPBR || !mat.lightRtOpenPBR.hasTextureInputs ||
      !mat.lightRtOpenPBR.hasNormalInput) {
    std::fprintf(stderr, "LightRT/OpenPBR texture flags were not baked\n");
    return 1;
  }
  if (!Near(mat.lightRtOpenPBR.baseColor[0], 1.0f) ||
      !Near(mat.lightRtOpenPBR.metalness, 1.0f) ||
      !Near(mat.lightRtOpenPBR.specularRoughness, 1.0f) ||
      !Near(mat.lightRtOpenPBR.emissionColor[0], 1.0f) ||
      !Near(mat.lightRtOpenPBR.emission, 3.0f) ||
      !Near(mat.lightRtOpenPBR.opacity, 1.0f) ||
      !Near(mat.lightRtOpenPBR.coatWeight, 1.0f) ||
      !Near(mat.lightRtOpenPBR.coatColor[0], 1.0f) ||
      !Near(mat.lightRtOpenPBR.coatColor[1], 1.0f) ||
      !Near(mat.lightRtOpenPBR.coatColor[2], 1.0f) ||
      !Near(mat.lightRtOpenPBR.coatRoughness, 1.0f) ||
      !Near(mat.lightRtOpenPBR.specularColor[0], 1.0f)) {
    std::fprintf(stderr, "LightRT/OpenPBR texture neutral factors are wrong\n");
    return 1;
  }
  if (!Near(mat.lightRtOpenPBR.specularAnisotropy, 0.6f) ||
      !Near(mat.lightRtOpenPBR.transmissionDispersion, 0.7f)) {
    std::fprintf(stderr,
                 "OpenPBR anisotropy/dispersion controls were not retained\n");
    return 1;
  }
  if (!draw.skipped.empty()) {
    std::fprintf(stderr, "supported OpenPBR surface lobes were diagnosed\n");
    return 1;
  }
  const tusdview::LoadDiagnostics diagnostics =
      tusdview::CategorizeLoadWarnings("", draw.skipped);
  if (diagnostics.unsupported_lobes != 0 || diagnostics.skipped != 0 ||
      diagnostics.actionable() != 0) {
    std::fprintf(stderr, "supported OpenPBR summary was not categorized\n");
    return 1;
  }

  // The live editor updates the canonical block and the legacy renderer
  // fallbacks together, while leaving texture-connected inspector params
  // alone. This is the backend-neutral contract used by GL/Vulkan/CUDA/HIP.
  tusdview::DrawMaterialCPU liveMat = mat;
  float connectedBaseParam = -1.0f;
  for (const tusdview::DrawMaterialParamCPU& param : liveMat.params) {
    if (param.shader == "OpenPBRSurface" && param.name == "base_color" &&
        (param.texture >= 0 || param.renderTexture >= 0)) {
      connectedBaseParam = param.value[0];
    }
  }
  tusdview::DrawLightRtOpenPBRCPU live = liveMat.lightRtOpenPBR;
  live.baseWeight = 0.37f;
  live.transmission = 0.61f;
  live.subsurface = 0.42f;
  live.specularRoughness = 0.18f;
  live.emission = 2.0f;
  live.emissionColor[0] = 0.25f;
  live.emissionColor[1] = 0.5f;
  live.emissionColor[2] = 0.75f;
  live.opacity = 0.65f;
  tusdview::ApplyOpenPBRMaterialConstants(&liveMat, live);
  if (!Near(liveMat.lightRtOpenPBR.baseWeight, 0.37f) ||
      !Near(liveMat.lightRtOpenPBR.transmission, 0.61f) ||
      !Near(liveMat.lightRtOpenPBR.subsurface, 0.42f) ||
      !Near(liveMat.roughness, 0.18f) || !Near(liveMat.emissive[0], 0.5f) ||
      !Near(liveMat.emissive[1], 1.0f) || !Near(liveMat.emissive[2], 1.5f) ||
      !Near(liveMat.alpha, 0.65f) ||
      liveMat.alphaMode != static_cast<int>(tusdview::AlphaMode::Blend)) {
    std::fprintf(stderr, "live OpenPBR constants were not synchronized\n");
    return 1;
  }
  if (connectedBaseParam >= 0.0f) {
    for (const tusdview::DrawMaterialParamCPU& param : liveMat.params) {
      if (param.shader == "OpenPBRSurface" && param.name == "base_color" &&
          (param.texture >= 0 || param.renderTexture >= 0) &&
          !Near(param.value[0], connectedBaseParam)) {
        std::fprintf(stderr,
                     "live OpenPBR edit rewrote a connected shader input\n");
        return 1;
      }
    }
  }

  // A dual-authored material can be converted into a session-local constant
  // OpenPBR variant for live tweaking. All shading connections are detached;
  // displacement remains a geometry concern and is intentionally untouched.
  tusdview::DrawMaterialCPU constantMat = mat;
  constantMat.hasUsdPreviewSurface = true;
  tusdview::MakeConstantOpenPBRMaterial(&constantMat);
  if (constantMat.hasUsdPreviewSurface ||
      !constantMat.openPbrSpecularModel ||
      constantMat.baseColorTex >= 0 || constantMat.metallicTex >= 0 ||
      constantMat.roughnessTex >= 0 || constantMat.normalTex >= 0 ||
      constantMat.emissiveTex >= 0 || constantMat.opacityTex >= 0 ||
      constantMat.materialXGraph.valid ||
      constantMat.lightRtOpenPBR.hasTextureInputs) {
    std::fprintf(stderr,
                 "constant OpenPBR variant retained a shading connection\n");
    return 1;
  }
  for (const tusdview::DrawMaterialParamCPU& param : constantMat.params) {
    if (param.shader == "OpenPBRSurface" &&
        (param.texture >= 0 || param.renderTexture >= 0 ||
         param.sample.tex >= 0)) {
      std::fprintf(stderr,
                   "constant OpenPBR variant retained a parameter texture\n");
      return 1;
    }
  }

  const tusdview::DrawMaterialCPU& graphMat = draw.materials[1];
  if (!graphMat.hasLightRtOpenPBR || graphMat.lightRtOpenPBR.hasTextureInputs) {
    std::fprintf(stderr, "MaterialX graph material was not baked as constants\n");
    return 1;
  }
  if (!graphMat.materialXGraph.valid ||
      graphMat.materialXGraph.nodes.size() != 1 ||
      graphMat.materialXGraph.output[0] != 0 ||
      graphMat.materialXGraph.nodes[0].op !=
          tusdview::MaterialXGraphOpCPU::Multiply) {
    std::fprintf(stderr,
                 "MaterialX graph was not compiled into runtime IR\n");
    return 1;
  }
  if (!Near(graphMat.lightRtOpenPBR.baseColor[0], 0.4f) ||
      !Near(graphMat.lightRtOpenPBR.baseColor[1], 0.2f) ||
      !Near(graphMat.lightRtOpenPBR.baseColor[2], 0.15f) ||
      !Near(graphMat.lightRtOpenPBR.metalness, 0.45f) ||
      !Near(graphMat.lightRtOpenPBR.specularRoughness, 0.25f) ||
      !Near(graphMat.lightRtOpenPBR.emission, 1.5f) ||
      !Near(graphMat.lightRtOpenPBR.opacity, 0.8f)) {
    std::fprintf(stderr, "MaterialX graph constants were not evaluated\n");
    return 1;
  }
  if (!Near(graphMat.baseColor[0], 0.4f) ||
      !Near(graphMat.baseColor[1], 0.2f) ||
      !Near(graphMat.baseColor[2], 0.15f) ||
      !Near(graphMat.metallic, 0.45f) ||
      !Near(graphMat.roughness, 0.25f) ||
      !Near(graphMat.alpha, 0.8f)) {
    std::fprintf(stderr, "MaterialX graph fallback was not copied to preview fields\n");
    return 1;
  }

  return 0;
}
