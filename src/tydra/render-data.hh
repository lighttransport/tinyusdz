// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// Render data structure suited for WebGL and Raytracing render

#include <unordered_map>

#include "nonstd/expected.hpp"
#include "value-types.hh"

namespace tinyusdz {

// forward decl
class Stage;
class Prim;
struct Material;
struct GeomMesh;
struct Xform;

template<typename T>
struct UsdPrimvarReader;

using UsdPrimvarReader_float2 = UsdPrimvarReader<value::float2>;

namespace tydra {

// GLSL like data types
using vec2 = value::float2;
using vec3 = value::float3;
using vec4 = value::float4;
using mat2 = value::matrix2f;  // float precision

enum class VertexVariability
{
  //Constant,
  //Uniform,
  //Varying,
  Vertex,
  FaceVarying,
  Indexed, // Need to supply index buffer
};

// Geometric, light and camera
enum class NodeType {
  Xform,
  Mesh,   // Polygon mesh
  PointLight,
  DomeLight,
  Camera,
  // TODO...
};

// glTF-like BufferData
struct BufferData {
  value::TypeId type_id{value::TypeId::TYPE_ID_VOID};
  std::vector<uint8_t> data;  // binary data

  // TODO: Stride
};

// glTF-like Attribute
struct Attribute {
  std::string path;     // Path string in Stage
  uint32_t slot_id{0};  // slot ID.

  int64_t buffer_id{-1};  // index to buffer_id
};

template<typename T>
struct VertexAttribute {
  std::vector<T> data;
  std::vector<uint32_t> indices; // indexed primvar(vertex attribute). Used when variability == Indexed
  VertexVariability variability;
  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

enum class ColorSpace {
  sRGB,
  Linear,
  Rec709,
  OCIO,
  Custom,  // TODO: Custom colorspace
};

// NOTE: Please distinguish with image::Image(src/image-types.hh). image::Image
// is much more generic image class. This `ImageData` class has typed pixel
// format and has colorspace info, which is suited for renderer/viewer/DCC
// backends.
template <typename T>
struct ImageData {
  std::vector<T> image;  // raw pixel data
  ColorSpace colorSpace{ColorSpace::sRGB};
  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1};  // e.g. 3 for RGB.

  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

// Simple LDR image
using LDRImage = ImageData<uint8_t>;

struct Node {
  NodeType nodeType{NodeType::Xform};

  int32_t id;  // Index to node content(e.g. meshes[id] when nodeTypes == Mesh

  std::vector<uint32_t> children;

  // Every node have its transform.
  value::matrix4d local_matrix;
  value::matrix4d global_matrix;

  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

struct RenderMesh {
  std::vector<vec3> points;
  std::vector<uint32_t> faceVertexIndices;
  std::vector<uint32_t> faceVertexCounts;

  // non-facevarying normal and texcoords are converted to facevarying
  std::vector<vec3> facevaryingNormals;

  // key = slot ID.
  std::unordered_map<uint32_t, Attribute> facevaryingTexcoords;

  std::vector<int32_t>
      materialIds;  // per-face material. -1 = no material assigned

  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

enum class UVReaderFloatComponentType
{
  COMPONENT_FLOAT,
  COMPONENT_FLOAT2,
  COMPONENT_FLOAT3,
  COMPONENT_FLOAT4,
};

// float, float2, float3 or float4 only
struct UVReaderFloat {
  UVReaderFloatComponentType componentType{UVReaderFloatComponentType::COMPONENT_FLOAT2};
  int32_t meshId{-1};   // index to RenderMesh
  int32_t coordId{-1};  // index to RenderMesh::facevaryingTexcoords

  mat2 transform; // UsdTransform2d

  // Returns interpolated UV coordinate with UV transform
  // # of components filled are equal to `componentType`.
  vec4 fetchUV(size_t faceId, float varyu, float varyv);
};

struct UVTexture {
  enum class Channel { R, G, B, RGB, RGBA };

  // TextureWrap `black` in UsdUVTexture is mapped to `CLAMP_TO_BORDER`(app must set border color to black)
  // default is CLAMP_TO_EDGE and `useMetadata` wrap mode is ignored.
  enum class WrapMode { CLAMP_TO_EDGE, REPEAT, MIRROR, CLAMP_TO_BORDER };

  WrapMode wrapS{WrapMode::CLAMP_TO_EDGE};
  WrapMode wrapT{WrapMode::CLAMP_TO_EDGE};

  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the
  // return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f,
             Channel channel = Channel::RGB);

  UVReaderFloat uvreader;

  int32_t imageId{-1};  // Index to Image
  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

struct UDIMTexture {
  enum class Channel { R, G, B, RGB, RGBA };

  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the
  // return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f,
             Channel channel = Channel::RGB);

  // key = UDIM id(e.g. 1001)
  std::unordered_map<uint32_t, int32_t> imageTileIds;
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

  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

// Material + Shader
struct RenderMaterial {
  std::string name;

  PreviewSurfaceShader shader;

  uint64_t handle{0}; // Handle ID for Graphics API. 0 = invalid
};

// Simple glTF-like Scene Graph
class RenderScene {
  std::vector<Node> nodes;
  std::vector<LDRImage> images;
  std::vector<RenderMaterial> materials;
  std::vector<UVTexture> textures;
  std::vector<RenderMesh> meshes;
  std::vector<BufferData>
      buffers;  // Various data storage(e.g. primvar texcoords)

  // uint32_t rootNodeId{0}; // root node = nodes[rootNodeId]
};

nonstd::expected<Node, std::string> Convert(const Stage &stage,
                                                  const Xform &xform);

nonstd::expected<RenderMesh, std::string> Convert(const Stage &stage,
                                                  const GeomMesh &mesh, bool triangulate=false);

// Currently float2 only
std::vector<UsdPrimvarReader_float2> ExtractPrimvarReadersFromMaterialNode(const Prim &node);

///
/// Convert USD Material/Shader to renderer-friendly Material
/// Assume single UsdPreviewSurface is assigned to a USD Material.
/// UVTexture and Images are managed by an array index,
/// so converted RenderMaterial, UVTexture and Images are appended to materials, textures, and images.
///
/// TODO: Support HDR image
///
bool ConvertMaterial(
  const Stage &stage,
  const tinyusdz::Material &material,
  std::vector<RenderMaterial> &materials, // [input]
  std::vector<UVTexture> &textures, // [inout]
  std::vector<LDRImage> &images); // [inout]


}  // namespace tydra
}  // namespace tinyusdz
