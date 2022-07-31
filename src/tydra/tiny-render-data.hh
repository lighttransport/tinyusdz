// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// Render data structure suited for WebGL and Raytracing render

#include <unordered_map>

#include "value-types.hh"

namespace tinyusdz {
namespace tydra {

// GLSL like data types
using vec2 = value::float2;
using vec3 = value::float3;
using vec4 = value::float4;
using mat2 = value::matrix2f; // float precision

enum class NodeType {
  Xform,
  Scope, // Node with no-op
  Mesh, // Polygon mesh
  Camera,
};

template<typename T>
struct Image {
  enum class ColorSpace {
    sRGB,
    Linear,
    Rec709,
    Custom, // TODO: Custom colorspace, OCIO colorspace
  };

  std::vector<T> image; // raw pixel data
  ColorSpace colorSpace{ColorSpace::sRGB};
  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1};  // e.g. 3 for RGB.
};

// Simple LDR image
using LDRImage = Image<uint8_t>;

struct Node {

  NodeType nodeType{NodeType::Xform};

  int32_t id; // Index to node content(e.g. meshes[id] when nodeTypes == Mesh

  std::vector<uint32_t> children;

  bool isScope{false};
};

// HdMeshTopology
struct RenderMesh
{
  std::vector<vec3> points;
  std::vector<uint32_t> faceVertexIndices;
  std::vector<uint32_t> faceVertexCounts;

  // non-facevarying normal and texcoords are converted to facevarying
  std::vector<vec3> facevaryingNormals;

  // key = uvmap ID.
  std::unordered_map<uint32_t, std::vector<vec3>> facevaryingTexcoords;

  std::vector<int32_t> materialIds; // per-face material. -1 = no material assigned
};

struct UVTexture {
  enum class Channel {
    R,
    G,
    B,
    RGB,
    RGBA
  };


  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f, Channel channel=Channel::RGB);

  int32_t imageId{-1}; // Index to Image
};


template<typename T>
struct UVReader {
  static_assert(std::is_same<T, float>::type || std::is_same<T, vec2>::type || std::is_same<T, vec3>::type || std::is_same<T, vec4>::type, "Type must be float, float2, float3 or float4");

  uint32_t meshId;   // index to RenderMesh
  uint32_t coordId;  // index to RenderMesh::facevaryingTexcoords

  mat2 transform;

  // Returns interpolated UV coordinate with UV transform
  T fetchUV(size_t faceId, float varyu, float varyv);

};

// base color(fallback color) or Texture
template <typename T>
struct ShaderParam {
  ShaderParam(const T &t) { value = t; }

  T value;
  int32_t textureId{-1};
};

// UsdPreviewSurface
struct PreviewSurfaceShader {
  bool useSpecularWorkFlow{false};

  ShaderParam<vec3> diffuseColor{{0.18f, 0.18f, 0.18f}};
  ShaderParam<float> metallic{0.0f};
  ShaderParam<float> roughness{0.5f};
  ShaderParam<float> clearcoat{0.0f};
  ShaderParam<float> clearcoatRoughness{0.01f};
  ShaderParam<float> opacity{1.0f};
  ShaderParam<float> opacityThreshold{0.0f};
  ShaderParam<float> ior{1.5f};
  ShaderParam<vec3> normal{{0.0f, 0.0f, 1.0f}};
  ShaderParam<float> displacement{0.0f};
  ShaderParam<float> occlusion{0.0f};
};


// Material + Shader
struct Material
{
  PreviewSurfaceShader shader;
};

// Simple glTF-like Scene Graph
class RenderScene
{
  std::vector<Node> nodes;
  std::vector<LDRImage> images;
  std::vector<Material> materials;
  std::vector<UVTexture> textures;
  std::vector<RenderMesh> meshes;

  //uint32_t rootNodeId{0}; // root node = nodes[rootNodeId]
};


} // namespace tydra
} // namespace tinyusdz

