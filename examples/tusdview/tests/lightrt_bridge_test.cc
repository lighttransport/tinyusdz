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
  mat.params.push_back(FloatParam("coat_roughness", 0.2f));
  mat.params.push_back(FloatParam("emission_luminance", 2.0f));
  mat.params.push_back(Vec3Param("emission_color", 0.1f, 0.2f, 0.3f));
  mat.params.push_back(FloatParam("opacity", 0.65f));
  tusdview::BakeLightRtOpenPBR(&mat);
  mat.baseColorSample.uv = {1.0f, 0.1f, 0.2f, 0.9f, 0.3f, 0.4f};
  mat.metallicSample.uv = {0.5f, 0.0f, 0.0f, 0.5f, 0.1f, 0.2f};
  mat.roughnessSample.uv = mat.metallicSample.uv;
  mat.normalSample.uv = {1.0f, 0.0f, 0.0f, 1.0f, 0.7f, 0.8f};
  mat.emissiveSample.uv = {0.25f, 0.0f, 0.0f, 0.25f, 0.2f, 0.3f};
  mat.displacementUv = {2.0f, 0.0f, 0.0f, 2.0f, -0.5f, 0.5f};
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
  mat.opacitySample.uvSet = 1;
  mat.opacitySample.uv.m00 = 1.5f;
  mat.opacitySample.uv.tx = 0.25f;
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
      !Near(directTexPack[66], 2.0f) || !Near(directTexPack[68], 0.1f)) {
    std::fprintf(stderr, "unexpected RT texture-param packing\n");
    return 1;
  }
  if (!Near(directRasterTexPack[8 * 4 + 0], 2.0f) ||
      !Near(directRasterTexPack[8 * 4 + 6], 0.5f) ||
      !Near(directRasterTexPack[16 * 4 + 0], 0.0f) ||
      !Near(directRasterTexPack[16 * 4 + 1], 3.0f) ||
      !Near(directRasterTexPack[17 * 4 + 2], 1.25f) ||
      !Near(directRasterTexPack[17 * 4 + 3], -0.25f) ||
      !Near(directRasterTexPack[20 * 4 + 0], 1.5f) ||
      !Near(directRasterTexPack[20 * 4 + 2], 0.25f) ||
      !Near(directRasterTexPack[22 * 4 + 0], 2.0f) ||
      !Near(directRasterTexPack[22 * 4 + 1], 0.8f) ||
      !Near(directRasterTexPack[22 * 4 + 2], 0.1f) ||
      !Near(directRasterTexPack[22 * 4 + 3], 1.0f) ||
      !Near(directRasterTexPack[24 * 4 + 0], 7.0f)) {
    std::fprintf(stderr, "unexpected raster texture-param packing\n");
    return 1;
  }
  if (host.cols.size() != 12 || !Near(host.cols[3], 0.25f) ||
      !Near(host.cols[7], 0.5f) || !Near(host.cols[11], 0.75f)) {
    std::fprintf(stderr, "displayOpacity RGBA packing changed\n");
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
  tinyusdz::tydra::LightRtOpenPBRParams sharedDefault;
  tinyusdz::tydra::PackLightRtOpenPBRParams(
      sharedDefault, false, 0.0f, 0.5f, sharedDefaultPack.data());
  for (int i = 0; i < tusdview::kLightRtOpenPBRFloats; ++i) {
    if (!Near(defaultPack[size_t(i)], sharedDefaultPack[size_t(i)])) {
      std::fprintf(stderr, "shared/default LightRT pack mismatch at %d\n", i);
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
  tusdview::DrawLightRtOpenPBRCPU graphEval;
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
  tusdview::DrawLightRtOpenPBRCPU standardGraphEval;
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

  return 0;
}
