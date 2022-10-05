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
struct GeomMesh;
struct Xform;

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
template <typename T, ColorSpace cs>
struct ImageData {
  std::vector<T> image;  // raw pixel data
  ColorSpace colorSpace{cs};
  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1};  // e.g. 3 for RGB.
};

// Simple LDR image
using LDRImage = ImageData<uint8_t, ColorSpace::sRGB>;

struct Node {
  NodeType nodeType{NodeType::Xform};

  int32_t id;  // Index to node content(e.g. meshes[id] when nodeTypes == Mesh

  std::vector<uint32_t> children;

  // Every node have its transform.
  value::matrix4d local_matrix;
  value::matrix4d global_matrix;
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
};

struct UVTexture {
  enum class Channel { R, G, B, RGB, RGBA };

  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the
  // return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f,
             Channel channel = Channel::RGB);

  int32_t imageId{-1};  // Index to Image
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

template <typename T>
struct UVReader {
  static_assert(std::is_same<T, float>::type || std::is_same<T, vec2>::type ||
                    std::is_same<T, vec3>::type || std::is_same<T, vec4>::type,
                "Type must be float, float2, float3 or float4");

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
struct Material {
  std::string name;

  PreviewSurfaceShader shader;
};

// Simple glTF-like Scene Graph
class RenderScene {
  std::vector<Node> nodes;
  std::vector<LDRImage> images;
  std::vector<Material> materials;
  std::vector<UVTexture> textures;
  std::vector<RenderMesh> meshes;
  std::vector<BufferData>
      buffers;  // Various data storage(e.g. primvar texcoords)

  // uint32_t rootNodeId{0}; // root node = nodes[rootNodeId]
};

#if 0  // TODO:
// result = (texture_id == -1) ? use color : lookup texture
struct Color3OrTexture {
  Color3OrTexture(float x, float y, float z) {
    color[0] = x;
    color[1] = y;
    color[2] = z;
  }

  std::array<float, 3> color{{0.0f, 0.0f, 0.0f}};

  std::string path;  // path to .connect(We only support texture file connection
                     // at the moment)
  int64_t texture_id{-1};

  bool HasTexture() const { return texture_id > -1; }
};

struct FloatOrTexture {
  FloatOrTexture(float x) { value = x; }

  float value{0.0f};

  std::string path;  // path to .connect(We only support texture file connection
                     // at the moment)
  int64_t texture_id{-1};

  bool HasTexture() const { return texture_id > -1; }
};

enum TextureWrap {
  TextureWrapUseMetadata,  // look for wrapS and wrapT metadata in the texture
                           // file itself
  TextureWrapBlack,
  TextureWrapClamp,
  TextureWrapRepeat,
  TextureWrapMirror
};

// For texture transform
// result = in * scale * rotate * translation
struct UsdTranform2d {
  float rotation =
      0.0f;  // counter-clockwise rotation in degrees around the origin.
  std::array<float, 2> scale{{1.0f, 1.0f}};
  std::array<float, 2> translation{{0.0f, 0.0f}};
};

// UsdUvTexture
struct UVTexture {
  std::string asset;  // asset name(usually file path)
  int64_t image_id{
      -1};  // TODO(syoyo): Consider UDIM `@textures/occlusion.<UDIM>.tex@`

  TextureWrap wrapS{};
  TextureWrap wrapT{};

  std::array<float, 4> fallback{
      {0.0f, 0.0f, 0.0f,
       1.0f}};  // fallback color used when texture cannot be read.
  std::array<float, 4> scale{
      {1.0f, 1.0f, 1.0f, 1.0f}};  // scale to be applied to output texture value
  std::array<float, 4> bias{
      {0.0f, 0.0f, 0.0f, 0.0f}};  // bias to be applied to output texture value

  UsdTranform2d texture_transfom;  // texture coordinate orientation.

  // key = connection name: e.g. "outputs:rgb"
  // item = pair<type, name> : example: <"float3", "outputs:rgb">
  std::map<std::string, std::pair<std::string, std::string>> outputs;

  PrimvarReaderType st;  // texture coordinate(`inputs:st`). We assume there is
                         // a connection to this.

  // TODO: orientation?
  // https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html#UsdPreviewSurfaceProposal-TextureCoordinateOrientationinUSD
};
#endif

nonstd::expected<Node, std::string> Convert(const Stage &stage,
                                                  const Xform &xform);

nonstd::expected<RenderMesh, std::string> Convert(const Stage &stage,
                                                  const GeomMesh &mesh);

}  // namespace tydra
}  // namespace tinyusdz
