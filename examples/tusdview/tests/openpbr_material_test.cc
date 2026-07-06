// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"

#include <cmath>
#include <cstdio>
#include <vector>

#include "tydra/render-data-shader.hh"
#include "tydra/render-data.hh"

namespace {

namespace tydra = tinyusdz::tydra;

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
  tydra::RenderScene scene;

  const int baseImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture);
  const int mrImage = AddImage(&scene, tydra::ColorSpace::Raw);
  const int emissiveImage = AddImage(&scene, tydra::ColorSpace::sRGB_Texture);
  const int normalImage = AddImage(&scene, tydra::ColorSpace::Raw);

  const int baseTex = AddTexture(&scene, baseImage, tydra::UVTexture::Channel::RGB);
  const int metalTex = AddTexture(&scene, mrImage, tydra::UVTexture::Channel::B);
  const int roughTex = AddTexture(&scene, mrImage, tydra::UVTexture::Channel::G);
  const int emissiveTex =
      AddTexture(&scene, emissiveImage, tydra::UVTexture::Channel::RGB);
  const int normalTex = AddTexture(&scene, normalImage, tydra::UVTexture::Channel::RGB);

  scene.textures[static_cast<size_t>(metalTex)].scale[2] = 0.25f;
  scene.textures[static_cast<size_t>(metalTex)].bias[2] = 0.10f;
  scene.textures[static_cast<size_t>(roughTex)].scale[1] = 0.50f;
  scene.textures[static_cast<size_t>(roughTex)].bias[1] = 0.20f;
  scene.textures[static_cast<size_t>(normalTex)].has_transform2d = true;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[0][0] = 2.0f;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[1][1] = 3.0f;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[2][0] = 0.25f;
  scene.textures[static_cast<size_t>(normalTex)].transform.m[2][1] = 0.5f;

  tydra::OpenPBRSurfaceShader shader;
  shader.base_color.value = {0.2f, 0.4f, 0.6f};
  shader.base_color.texture_id = baseTex;
  shader.base_metalness.value = 0.5f;
  shader.base_metalness.texture_id = metalTex;
  shader.base_roughness.value = 0.35f;
  shader.base_roughness.texture_id = roughTex;
  shader.normal.texture_id = normalTex;
  shader.coat_normal.texture_id = normalTex;
  shader.emission_luminance.value = 3.0f;
  shader.emission_color.value = {0.1f, 0.2f, 0.3f};
  shader.emission_color.texture_id = emissiveTex;
  shader.opacity.value = 0.7f;

  tydra::RenderMaterial material;
  material.name = "openpbr_textured";
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
  if (draw.textures.size() != 4) {
    std::fprintf(stderr, "expected four deduplicated textures, got %zu\n",
                 draw.textures.size());
    return 1;
  }
  if (draw.textures[0].assetIdentifier != "asset_0.png" ||
      draw.textures[0].renderImageId != baseImage ||
      draw.textures[3].assetIdentifier != "asset_3.png" ||
      draw.textures[3].renderImageId != normalImage) {
    std::fprintf(stderr, "texture source metadata was not preserved\n");
    return 1;
  }

  const tusdview::DrawMaterialCPU& mat = draw.materials[0];
  if (mat.baseColorTex < 0 || mat.metalRoughTex < 0 || mat.normalTex < 0 ||
      mat.coatNormalTex < 0 || mat.emissiveTex < 0) {
    std::fprintf(stderr, "OpenPBR texture slots were not populated\n");
    return 1;
  }
  if (mat.metalRoughTex != mat.baseColorTex + 1) {
    std::fprintf(stderr, "unexpected metal/rough texture dedup order\n");
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
      !Near(mat.alpha, 0.7f)) {
    std::fprintf(stderr, "OpenPBR textured neutral factors are wrong\n");
    return 1;
  }
  if (!Near(mat.normalSample.scale[0], 2.0f) ||
      !Near(mat.normalSample.bias[0], -1.0f)) {
    std::fprintf(stderr, "OpenPBR normal texture unpack defaults are wrong\n");
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
  if (mat.coatNormalTex != mat.normalTex ||
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
      !Near(mat.lightRtOpenPBR.emission, 3.0f)) {
    std::fprintf(stderr, "LightRT/OpenPBR texture neutral factors are wrong\n");
    return 1;
  }

  const tusdview::DrawMaterialCPU& graphMat = draw.materials[1];
  if (!graphMat.hasLightRtOpenPBR || graphMat.lightRtOpenPBR.hasTextureInputs) {
    std::fprintf(stderr, "MaterialX graph material was not baked as constants\n");
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
