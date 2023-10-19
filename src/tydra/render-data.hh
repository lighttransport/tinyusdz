// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// Render data structure suited for WebGL and Raytracing render

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "asset-resolution.hh"
#include "nonstd/expected.hpp"
#include "usdShade.hh"
#include "value-types.hh"

namespace tinyusdz {

// forward decl
class Stage;
class Prim;
struct Material;
struct GeomMesh;
struct Xform;
struct AssetInfo;
class Path;

struct UsdPreviewSurface;
struct UsdUVTexture;

template <typename T>
struct UsdPrimvarReader;

using UsdPrimvarReader_float2 = UsdPrimvarReader<value::float2>;

namespace tydra {

// GLSL like data types
using vec2 = value::float2;
using vec3 = value::float3;
using vec4 = value::float4;
using quat = value::float4;
using mat2 = value::matrix2f;   // float precision
using mat3 = value::matrix3f;   // float precision
using mat4 = value::matrix4f;   // float precision
using dmat4 = value::matrix4d;  // float precision

// Simple string <-> id map
struct StringAndIdMap {
  void add(uint64_t key, const std::string &val) {
    _i_to_s[key] = val;
    _s_to_i[val] = key;
  }

  void add(const std::string &key, uint64_t val) {
    _s_to_i[key] = val;
    _i_to_s[val] = key;
  }

  size_t count(uint64_t i) const { return _i_to_s.count(i); }

  size_t count(const std::string &s) const { return _s_to_i.count(s); }

  std::string at(uint64_t i) const { return _i_to_s.at(i); }

  uint64_t at(std::string s) const { return _s_to_i.at(s); }

  std::map<uint64_t, std::string>::const_iterator find(uint64_t key) const {
    return _i_to_s.find(key);
  }

  std::map<std::string, uint64_t>::const_iterator find(
      const std::string &key) const {
    return _s_to_i.find(key);
  }

  std::map<std::string, uint64_t>::const_iterator s_begin() const {
    return _s_to_i.begin();
  }

  std::map<std::string, uint64_t>::const_iterator s_end() const {
    return _s_to_i.end();
  }

  std::map<uint64_t, std::string>::const_iterator i_begin() const {
    return _i_to_s.begin();
  }

  std::map<uint64_t, std::string>::const_iterator i_end() const {
    return _i_to_s.end();
  }

  size_t size() const {
    // size should be same, but just in case.
    if (_i_to_s.size() == _s_to_i.size()) {
      return _i_to_s.size();
    }

    return 0;
  }

  std::map<uint64_t, std::string> _i_to_s;  // index -> string
  std::map<std::string, uint64_t> _s_to_i;  // string -> index
};

// timeSamples in USD
// TODO: AttributeBlock support
template <typename T>
struct AnimationSample {
  float t{0.0};  // time is represented as float
  T value;
};

enum class VertexVariability {
  Constant,
  Uniform,
  Varying,  // per-vertex
  Vertex,   // basically per-vertex(it depends on Subdivision scheme)
  FaceVarying,
  Indexed,  // Need to supply index buffer
};

std::string to_string(VertexVariability variability);

// Geometric, light and camera
enum class NodeType {
  Xform,
  Mesh,  // Polygon mesh
  PointLight,
  DomeLight,
  Camera,
  // TODO...
};

enum class ComponentType {
  UInt8,
  Int8,
  UInt16,
  Int16,
  UInt32,
  Int32,
  Half,
  Float,
  Double,
};

std::string to_string(ComponentType ty);

// glTF-like BufferData
struct BufferData {
  ComponentType componentType{ComponentType::UInt8};
  uint8_t count{1};           // up to 256
  std::vector<uint8_t> data;  // binary data

  // TODO: Stride
};

// glTF-like Attribute
struct Attribute {
  std::string path;     // Path string in Stage
  uint32_t slot_id{0};  // slot ID.

  int64_t buffer_id{-1};  // index to buffer_id
};

// TODO: Support more format
enum class VertexAttributeFormat {
  Float,   // float
  Vec2,    // float2
  Vec3,    // float3
  Vec4,    // float4
  Ivec2,   // int2
  Uvec4,   // uint4
  Double,  // double
  Dvec2,   // double2
  Dvec3,   // double3
  Dvec4,   // double4
};

std::string to_string(VertexAttributeFormat f);

struct VertexAttribute {
  VertexAttributeFormat format{VertexAttributeFormat::Vec3};
  uint32_t stride{
      0};  //  We don't support packed(interleaved) vertex data, so stride is
           //  usually sizeof(VertexAttributeFormat type). 0 = tightly packed.
           //  Let app/gfx API decide actual stride bytes.
  std::vector<uint8_t> data;  // raw binary data(TODO: Use Buffer ID?)
  std::vector<uint32_t>
      indices;  // Dedicated Index buffer. Set when variability == Indexed.
                // empty = Use vertex index buffer
  VertexVariability variability{VertexVariability::FaceVarying};
  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid

  size_t counts() const {
    if (stride != 0) {
      return data.size() / stride;
    }

    size_t elemsize = 0;

    switch (format) {
      case VertexAttributeFormat::Float: {
        elemsize = 4;
        break;
      }
      case VertexAttributeFormat::Vec2: {
        elemsize = sizeof(float) * 2;
        break;
      }
      case VertexAttributeFormat::Vec3: {
        elemsize = sizeof(float) * 3;
        break;
      }
      case VertexAttributeFormat::Vec4: {
        elemsize = sizeof(float) * 4;
        break;
      }
      case VertexAttributeFormat::Ivec2: {
        elemsize = sizeof(int) * 2;
        break;
      }
      case VertexAttributeFormat::Uvec4: {
        elemsize = sizeof(int) * 4;
        break;
      }
      case VertexAttributeFormat::Double: {
        elemsize = sizeof(double);
        break;
      }
      case VertexAttributeFormat::Dvec2: {
        elemsize = sizeof(double) * 2;
        break;
      }
      case VertexAttributeFormat::Dvec3: {
        elemsize = sizeof(double) * 3;
        break;
      }
      case VertexAttributeFormat::Dvec4: {
        elemsize = sizeof(double) * 4;
        break;
      }
    }

    if (elemsize == 0) {  // this should not happen though.
      return 0;
    }

    return data.size() / elemsize;
  }
};

enum class ColorSpace {
  sRGB,
  Linear,
  Rec709,
  OCIO,
  Custom,  // TODO: Custom colorspace
};

std::string to_string(ColorSpace cs);

struct TextureImage {
  std::string asset_identifier;  // (resolved) filename or asset identifier.

  ComponentType texelComponentType{
      ComponentType::UInt8};  // texel bit depth of `buffer_id`
  ComponentType assetTexelComponentType{
      ComponentType::UInt8};  // texel bit depth of UsdUVTexture asset

  ColorSpace colorSpace{ColorSpace::sRGB};  // color space of texel data.
  ColorSpace usdColorSpace{
      ColorSpace::sRGB};  // original color space info in UsdUVTexture

  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1};  // e.g. 3 for RGB.
  int32_t miplevel{0};

  int64_t buffer_id{-1};  // index to buffer_id(texel data)

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

// glTF-lie animation data

template <typename T>
struct AnimationSampler {
  std::vector<AnimationSample<T>> samples;

  // No cubicSpline
  enum class Interpolation {
    Linear,
    Step,  // Held in USD
  };

  Interpolation interpolation{Interpolation::Linear};
};

struct AnimationChannel {
  enum class ChannelType { Transform, Translation, Rotation, Scale };

  // Matrix precision is recuded to float-precision
  // NOTE: transform is not supported in glTF(you need to decompose transform
  // matrix into TRS)
  AnimationSampler<mat4> transforms;

  // half-types are upcasted to float precision
  AnimationSampler<vec3> translations;
  AnimationSampler<quat> rotations;  // Rotation is converted to quaternions
  AnimationSampler<vec3> scales;

  int64_t taget_node{-1};  // array index to RenderScene::nodes
};

struct Animation {
  std::string path;  // USD Prim path
  std::vector<AnimationChannel> channels;
};

struct Node {
  NodeType nodeType{NodeType::Xform};

  int32_t id;  // Index to node content(e.g. meshes[id] when nodeTypes == Mesh

  std::vector<uint32_t> children;

  // Every node have its transform at `default` timecode.
  value::matrix4d local_matrix;
  value::matrix4d global_matrix;

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

// Currently normals and texcoords are converted as facevarying attribute.
struct RenderMesh {
  std::string element_name;  // element(leaf) Prim name
  std::string abs_name;      // absolute Prim path in USD

  std::vector<vec3> points;
  std::vector<uint32_t> faceVertexIndices;
  // For triangulated mesh, array elements are all 3.
  // TODO: Make `faceVertexCounts` empty for Trianglulated mesh.
  std::vector<uint32_t> faceVertexCounts;

  std::vector<vec3> facevaryingNormals;
  Interpolation normalsInterpolation;  // Optional info. USD interpolation for
                                       // `facevaryingNormals`

  // key = slot ID.
  // vec2(texCoord2f) only
  // TODO: Interpolation for UV
  std::unordered_map<uint32_t, std::vector<vec2>> facevaryingTexcoords;

  std::vector<int32_t>
      materialIds;  // per-face material. -1 = no material assigned

  std::map<uint32_t, VertexAttribute> primvars;  // Excludes texcoords

  // Index value = key to `primvars`
  StringAndIdMap primvarsMap;

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

enum class UVReaderFloatComponentType {
  COMPONENT_FLOAT,
  COMPONENT_FLOAT2,
  COMPONENT_FLOAT3,
  COMPONENT_FLOAT4,
};

std::string to_string(UVReaderFloatComponentType ty);

// float, float2, float3 or float4 only
struct UVReaderFloat {
  UVReaderFloatComponentType componentType{
      UVReaderFloatComponentType::COMPONENT_FLOAT2};
  int64_t mesh_id{-1};   // index to RenderMesh
  int64_t coord_id{-1};  // index to RenderMesh::facevaryingTexcoords

  // mat2 transform; // UsdTransform2d

  // Returns interpolated UV coordinate with UV transform
  // # of components filled are equal to `componentType`.
  vec4 fetchUV(size_t faceId, float varyu, float varyv);
};

struct UVTexture {
  enum class Channel { R, G, B, RGB, RGBA };

  // TextureWrap `black` in UsdUVTexture is mapped to `CLAMP_TO_BORDER`(app must
  // set border color to black) default is CLAMP_TO_EDGE and `useMetadata` wrap
  // mode is ignored.
  enum class WrapMode { CLAMP_TO_EDGE, REPEAT, MIRROR, CLAMP_TO_BORDER };

  WrapMode wrapS{WrapMode::CLAMP_TO_EDGE};
  WrapMode wrapT{WrapMode::CLAMP_TO_EDGE};

  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the
  // return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f,
             Channel channel = Channel::RGB);

  // bias and scale for texel value
  vec4 bias{0.0f, 0.0f, 0.0f, 0.0f};
  vec4 scale{1.0f, 1.0f, 1.0f, 1.0f};

  UVReaderFloat uvreader;
  vec4 fallback_uv{0.0f, 0.0f, 0.0f, 0.0f};

  // UsdTransform2d
  // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
  // = scale * rotate + translation
  bool has_transform2d{false};  // true = `transform`, `tx_rotation`, `tx_scale`
                                // and `tx_translation` are filled;
  mat3 transform{value::matrix3f::identity()};

  // raw transform2d value
  float tx_rotation{0.0f};
  vec2 tx_scale{1.0f, 1.0f};
  vec2 tx_translation{0.0f, 0.0f};

  // UV primvars name(UsdPrimvarReader's inputs:varname)
  std::string varname_uv;

  int64_t texture_image_id{-1};  // Index to TextureImage
  uint64_t handle{0};            // Handle ID for Graphics API. 0 = invalid
};

std::string to_string(UVTexture::WrapMode ty);

struct UDIMTexture {
  enum class Channel { R, G, B, RGB, RGBA };

  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the
  // return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f,
             Channel channel = Channel::RGB);

  // key = UDIM id(e.g. 1001)
  std::unordered_map<uint32_t, int32_t> imageTileIds;
};

// T or TextureId
template <typename T>
struct ShaderParam {
  ShaderParam(const T &t) { value = t; }

  bool is_texture() const { return textureId >= 0; }

  template <typename STy>
  void set_value(const STy &val) {
    // Currently we assume T == Sty.
    // TODO: support more type variant
    static_assert(value::TypeTraits<T>::underlying_type_id() ==
                      value::TypeTraits<STy>::underlying_type_id(),
                  "");
    static_assert(sizeof(T) >= sizeof(STy), "");
    memcpy(&value, &val, sizeof(T));
  }

  T value;
  int32_t textureId{-1};  // negative = invalid
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

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

// Material + Shader
struct RenderMaterial {
  std::string name;  // elementName in USD (e.g. "pbrMat")
  std::string
      abs_path;  // abosolute Prim path in USD (e.g. "/_material/scope/pbrMat")

  PreviewSurfaceShader surfaceShader;
  // TODO: displacement, volume.

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

// Simple glTF-like Scene Graph
class RenderScene {
 public:
  std::vector<Node> nodes;  // Prims in USD
  std::vector<TextureImage> images;
  std::vector<RenderMaterial> materials;
  std::vector<UVTexture> textures;
  std::vector<RenderMesh> meshes;
  std::vector<Animation> animations;
  std::vector<BufferData>
      buffers;  // Various data storage(e.g. primvar texcoords)

  // int64_t default_root_node{-1}; // index to `nodes`. `defaultPrim` in USD
};

///
/// Texture image loader callback
///
/// The callback function should return TextureImage and Raw image data.
///
/// NOTE: TextureImage::buffer_id is filled in Tydra side after calling this
/// callback. NOTE: TextureImage::colorSpace will be overwritten if
/// `asset:sourceColorSpace` is authored in UsdUVTexture.
///
/// @param[in] asset Asset path
/// @param[in] assetInfo AssetInfo
/// @param[in] assetResolver AssetResolutionResolver context. Please pass
/// DefaultAssetResolutionResolver() if you don't have custom
/// AssetResolutionResolver.
/// @param[out] texImageOut TextureImage info.
/// @param[out] imageData Raw texture image data.
/// @param[inout] userdata User data.
/// @param[out] warn Optional. Warning message.
/// @param[out] error Optional. Error message.
///
/// @return true upon success.
/// termination of visiting Prims.
///
typedef bool (*TextureImageLoaderFunction)(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    AssetResolutionResolver &assetResolver, TextureImage *imageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err);

bool DefaultTextureImageLoaderFunction(const value::AssetPath &assetPath,
                                       const AssetInfo &assetInfo,
                                       AssetResolutionResolver &assetResolver,
                                       TextureImage *imageOut,
                                       std::vector<uint8_t> *imageData,
                                       void *userdata, std::string *warn,
                                       std::string *err);

///
/// TODO: UDIM loder
///

#if 0  // TODO: remove
///
/// Easy API to convert USD Stage to RenderScene(glTF-like scene graph)
///
bool ConvertToRenderScene(
  const Stage &stage,
  RenderScene *scene,
  std::string *warn,
  std::string *err);

nonstd::expected<Node, std::string> Convert(const Stage &stage,
                                                  const Xform &xform);

nonstd::expected<RenderMesh, std::string> Convert(const Stage &stage,
                                                  const GeomMesh &mesh, bool triangulate=false);

// Currently float2 only
std::vector<UsdPrimvarReader_float2> ExtractPrimvarReadersFromMaterialNode(const Prim &node);
#endif

struct MeshConverterConfig {
  bool triangulate{true};
};

struct MaterialConverterConfig {
  // DefaultTextureImageLoader will be used when nullptr;
  TextureImageLoaderFunction texture_image_loader_function{nullptr};
  void *texture_image_loader_function_userdata{nullptr};

  // For UsdUVTexture.
  //
  // Default configuration:
  //
  // - The converter converts 8bit texture to floating point image and texel
  // data is converted to linear space.
  // - Allow missing asset(texture) and asset load failure.
  //
  // Recommended configuration for mobile/WebGL
  //
  // - `preserve_texel_bitdepth` true
  // - `linearize_color_space` true
  //   - No sRGB -> Linear conversion in a shader

  // In the UsdUVTexture spec, 8bit texture image is converted to floating point
  // image of range `[0.0, 1.0]`. When this flag is set to false, 8bit and 16bit
  // texture image is converted to floating point image. When this flag is set to true,
  // 8bit and 16bit texture data is stored as-is to save memory usage. Setting
  // true is good if you want to render USD scene on mobile, WebGL, etc.
  bool preserve_texel_bitdepth{false};

  // Apply the inverse of a color space to make texture image in linear space.
  // When `preserve_texel_bitdepth` is set to true, linearization also preserse texel bit depth
  // (i.e, for 8bit sRGB image, 8bit linear-space image is produced) 
  bool linearize_color_space{false};

  // Allow asset(texture, shader, etc) path with Windows backslashes(e.g.
  // ".\textures\cat.png")? When true, convert it to forward slash('/') on
  // Posixish system.
  bool allow_backslash_in_asset_path{true};

  // Allow texture load failure?
  bool allow_texture_load_failure{true};

  // Allow asset(e.g. texture file/shader file) which does not exit?
  bool allow_missing_asset{true};

  // ------------------------------------------
};

struct RenderSceneConverterConfig {
  // Load texture image data on convert.
  // false: no actual texture file/asset access.
  // App/User must setup TextureImage manually after the conversion.
  bool load_texture_assets{true};
};

class RenderSceneConverter {
 public:
  RenderSceneConverter() = default;
  RenderSceneConverter(const RenderSceneConverter &rhs) = delete;
  RenderSceneConverter(RenderSceneConverter &&rhs) = delete;

  void set_scene_config(const RenderSceneConverterConfig &config) {
    _scene_config = config;
  }

  void set_mesh_config(const MeshConverterConfig &config) {
    _mesh_config = config;
  }

  void set_material_config(const MaterialConverterConfig &config) {
    _material_config = config;
  }

  void set_asset_resoluition_resolver(AssetResolutionResolver &&rhs) {
    _asset_resolver = std::move(rhs);
  }

  void set_search_paths(const std::vector<std::string> &paths) {
    _asset_resolver.set_search_paths(paths);
  }

  ///
  /// Convert Stage to RenderScene.
  /// Must be called after SetStage, SetMaterialConverterConfig(optional)
  ///
  bool ConvertToRenderScene(const Stage &stage, RenderScene *scene);

  const std::string &GetInfo() const { return _info; }
  const std::string &GetWarning() const { return _warn; }
  const std::string &GetError() const { return _err; }

  StringAndIdMap nodeMap;
  StringAndIdMap meshMap;
  StringAndIdMap materialMap;
  StringAndIdMap textureMap;
  StringAndIdMap imageMap;
  StringAndIdMap bufferMap;
  std::vector<Node> nodes;
  std::vector<RenderMesh> meshes;
  std::vector<RenderMaterial> materials;
  std::vector<UVTexture> textures;
  std::vector<TextureImage> images;
  std::vector<BufferData> buffers;

  ///
  /// @param[in] rmaterial_id RenderMaterial index. -1 if no material assigned
  /// to this Mesh. If the mesh has bounded material, RenderMaterial index must
  /// be obrained using ConvertMaterial method.
  /// @param[in] mesh Input GeomMesh
  /// @param[out] dst RenderMesh output
  ///
  /// @return true when success.
  ///
  /// TODO: per-face material(GeomSubset)
  ///
  bool ConvertMesh(const int64_t rmaterial_d, const tinyusdz::GeomMesh &mesh,
                   RenderMesh *dst);

  ///
  /// Convert USD Material/Shader to renderer-friendly Material
  ///
  /// @return true when success.
  ///
  bool ConvertMaterial(const tinyusdz::Path &abs_mat_path,
                       const tinyusdz::Material &material,
                       RenderMaterial *rmat_out);

  bool ConvertPreviewSurfaceShader(const tinyusdz::Path &shader_abs_path,
                                   const tinyusdz::UsdPreviewSurface &shader,
                                   PreviewSurfaceShader *pss_out);

  bool ConvertUVTexture(const Path &tex_abs_path, const AssetInfo &assetInfo, const UsdUVTexture &texture,
                        UVTexture *tex_out);

  const Stage *GetStagePtr() const { return _stage; }

 private:
  template <typename T, typename Dty>
  bool ConvertPreviewSurfaceShaderParam(
      const Path &shader_abs_path,
      const TypedAttributeWithFallback<Animatable<T>> &param,
      const std::string &param_name, ShaderParam<Dty> &dst_param);

  AssetResolutionResolver _asset_resolver;

  RenderSceneConverterConfig _scene_config;
  MeshConverterConfig _mesh_config;

  MaterialConverterConfig _material_config;
  const Stage *_stage{nullptr};

  void PushInfo(const std::string &msg) { _info += msg; }
  void PushWarn(const std::string &msg) { _warn += msg; }
  void PushError(const std::string &msg) { _err += msg; }

  std::string _info;
  std::string _err;
  std::string _warn;
};

// For debug
// Supported format: "kdl" (default. https://kdl.dev/), "json"
//
std::string DumpRenderScene(const RenderScene &scene,
                            const std::string &format = "kdl");

}  // namespace tydra
}  // namespace tinyusdz
