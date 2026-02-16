// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-data.hh
/// @brief Tydra render-friendly data structures and conversion utilities
///
/// The Tydra framework converts USD scene graphs into render-friendly
/// data structures optimized for graphics APIs like OpenGL/WebGL, Vulkan,
/// and raytracing engines. This header defines the core RenderScene and
/// related data structures.
///
/// Key features:
/// - Flattened scene representation with pre-computed transforms
/// - GPU-friendly data layout (vertex buffers, index buffers)
/// - Material and texture management
/// - Animation support
/// - Multiple rendering backend support (WebGL, Vulkan, raytracing)
///
/// Main classes:
/// - RenderScene: Top-level render scene container
/// - RenderMesh: GPU-ready mesh data with materials
/// - RenderMaterial: Processed material definitions
/// - RenderTexture: Texture resources and samplers
/// - RenderCamera: Camera parameters for rendering
/// - RenderLight: Light definitions for shading
///
/// Memory optimization:
/// - Define TYDRA_USE_INDEX to use array indices instead of values in
///   DefaultPackedVertexData, reducing memory usage by ~55% per vertex
/// - When TYDRA_USE_INDEX is defined, attributes are stored as uint32_t
///   indices into shared attribute arrays instead of duplicate values
/// - Use index value ~0u (UINT32_MAX) to indicate missing attributes
///
/// Epsilon-based comparison:
/// - DefaultPackedVertexDataCompare and DefaultPackedVertexDataEqualEps
///   provide floating-point comparison with configurable epsilon values
/// - Default epsilon: 1e-6f for positions, 1e-3f for other attributes
/// - Supports both indexed and direct value modes
/// - Use these comparators for robust vertex deduplication with floating-point data
///
/// Spatial hashing optimization:
/// - VertexSpatialHashGrid provides O(1) average-case vertex similarity search
/// - Uses Morton code ordering for cache-friendly traversal
/// - BuildIndicesWithSpatialHash() offers optimized vertex deduplication
/// - Particularly beneficial for large meshes (>10K vertices)
/// - Automatic subdivision for large cells maintains performance
///
/// The conversion process:
/// ```cpp
/// tinyusdz::tydra::RenderScene renderScene;
/// tinyusdz::tydra::RenderSceneConverter converter;
/// bool success = converter.ConvertToRenderScene(stage, &renderScene);
/// ```
///
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "asset-resolution.hh"
#include "nonstd/expected.hpp"
#include "typed-array.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "value-types.hh"

// tydra
#include "common-types.hh"
#include "scene-access.hh"
#include "variant-support.hh"
#include "spatial-hashes.hh"
#include "render-data-pprint.hh"

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

using UsdPrimvarReader_int = UsdPrimvarReader<int>;
using UsdPrimvarReader_float = UsdPrimvarReader<float>;
using UsdPrimvarReader_float3 = UsdPrimvarReader<value::float3>;
using UsdPrimvarReader_float3 = UsdPrimvarReader<value::float3>;
using UsdPrimvarReader_string = UsdPrimvarReader<std::string>;
using UsdPrimvarReader_matrix4d = UsdPrimvarReader<value::matrix4d>;

namespace tydra {

///
/// Progress callback function type for RenderSceneConverter.
/// @param[in] progress Progress value between 0.0 and 1.0
/// @param[in] userptr User-provided pointer for custom data
/// @return true to continue conversion, false to cancel
///
using ProgressCallback = std::function<bool(float progress, void *userptr)>;

///
/// Detailed progress information for fine-grained progress reporting.
/// Contains counts for meshes, materials, textures and the current processing stage.
///
struct DetailedProgressInfo {
  enum class Stage {
    Idle,
    CountingPrims,      // Counting prims before conversion
    ConvertingXforms,   // Converting xform nodes
    ConvertingMeshes,   // Converting meshes
    ConvertingMaterials,// Converting materials
    ConvertingTextures, // Loading textures
    BuildingHierarchy,  // Building node hierarchy
    ExtractingAnimations,// Extracting animations
    MergingMeshes,      // Merging meshes (optional)
    Complete
  };

  Stage stage{Stage::Idle};
  float progress{0.0f};           // 0.0 to 1.0 overall progress

  // Mesh progress
  size_t meshes_processed{0};
  size_t meshes_total{0};
  std::string current_mesh_name;

  // Material progress
  size_t materials_processed{0};
  size_t materials_total{0};
  std::string current_material_name;

  // Texture progress
  size_t textures_processed{0};
  size_t textures_total{0};
  std::string current_texture_name;

  // Generic progress message
  std::string message;

  const char* GetStageName() const {
    switch (stage) {
      case Stage::Idle: return "idle";
      case Stage::CountingPrims: return "counting";
      case Stage::ConvertingXforms: return "xforms";
      case Stage::ConvertingMeshes: return "meshes";
      case Stage::ConvertingMaterials: return "materials";
      case Stage::ConvertingTextures: return "textures";
      case Stage::BuildingHierarchy: return "hierarchy";
      case Stage::ExtractingAnimations: return "animations";
      case Stage::MergingMeshes: return "merging";
      case Stage::Complete: return "complete";
    }
    return "unknown";
  }
};

///
/// Detailed progress callback function type for RenderSceneConverter.
/// Provides more granular progress information including mesh/material counts.
/// @param[in] info Detailed progress information
/// @param[in] userptr User-provided pointer for custom data
/// @return true to continue conversion, false to cancel
///
using DetailedProgressCallback = std::function<bool(const DetailedProgressInfo &info, void *userptr)>;

// Conditional typedef for ChunkedVectorArray based on TYDRA_USE_CHUNKED_ARRAY
#ifdef TYDRA_USE_CHUNKED_ARRAY
template<typename T>
using ChunkedVectorArray = tinyusdz::ChunkedTypedArray<T>;
#else
template<typename T>
using ChunkedVectorArray = std::vector<T>;
#endif

// GLSL like data types
using vec2 = value::float2;
using vec3 = value::float3;
using vec4 = value::float4;
using quat = value::float4; // (x, y, z, w)
using mat2 = value::matrix2f;
using mat3 = value::matrix3f;
using mat4 = value::matrix4f;
using dmat4 = value::matrix4d;

///
/// Bidirectional mapping between strings and numeric IDs.
/// Useful for converting between human-readable names and efficient
/// numeric identifiers in render data structures.
///
struct StringAndIdMap {
  using IdToStringMap = std::unordered_map<uint64_t, std::string>;
  using StringToIdMap = std::unordered_map<std::string, uint64_t>;
  using id_const_iterator = IdToStringMap::const_iterator;
  using string_const_iterator = StringToIdMap::const_iterator;

  void add(uint64_t key, const std::string &val) {
    _i_to_s[key] = val;
    _s_to_i[val] = key;
  }

  void add(const std::string &key, uint64_t val) {
    _s_to_i[key] = val;
    _i_to_s[val] = key;
  }

  bool empty() const { return _i_to_s.empty(); }

  size_t count(uint64_t i) const { return _i_to_s.count(i); }

  size_t count(const std::string &s) const { return _s_to_i.count(s); }

  std::string at(uint64_t i) const { return _i_to_s.at(i); }

  uint64_t at(const std::string &s) const { return _s_to_i.at(s); }

  id_const_iterator find(uint64_t key) const {
    return _i_to_s.find(key);
  }

  string_const_iterator find(const std::string &key) const {
    return _s_to_i.find(key);
  }

  string_const_iterator s_begin() const {
    return _s_to_i.begin();
  }

  string_const_iterator s_end() const {
    return _s_to_i.end();
  }

  id_const_iterator i_begin() const {
    return _i_to_s.begin();
  }

  id_const_iterator i_end() const {
    return _i_to_s.end();
  }

  size_t size() const {
    // size should be same, but just in case.
    if (_i_to_s.size() == _s_to_i.size()) {
      return _i_to_s.size();
    }

    return 0;
  }

  IdToStringMap _i_to_s;  // index -> string
  StringToIdMap _s_to_i;  // string -> index
};

// timeSamples in USD
// TODO: AttributeBlock support
template <typename T>
struct AnimationSample {
  float t{0.0};  // time is represented as float
  T value;
};

enum class VertexVariability {
  Constant,  // one value for all geometric elements
  Uniform,   // one value for each geometric elements(e.g. `face`, `UV patch`)
  Varying,   // per-vertex for each geometric elements. Bilinear interpolation.
  Vertex,  // Equvalent to `Varying` for Polygon mesh. The basis function of the
           // surface is used for the interpolation(Curves, Subdivision Surface,
           // etc).
  FaceVarying,  // per-Vertex per face. Bilinear interpolation.
  Indexed,      // Dedicated index buffer provided(unflattened Indexed Primvar).
};

//std::string to_string(VertexVariability variability);

enum class NodeType {
  Xform,
  Mesh,  // Polygon mesh
  Camera,
  SkelRoot, // UsdSkelRoot: encapsulation prim for skinned subtree
  Skeleton, // UsdSkeleton: joint hierarchy with bind/rest transforms
  PointLight,       // SphereLight in USD
  DirectionalLight, // DistantLight in USD
  EnvmapLight,      // DomeLight in USD
  RectLight,
  DiskLight,
  CylinderLight,
  GeometryLight,
};

// High-level categorization of USD Prim types
enum class NodeCategory {
  Group,     // Organizational: Xform, Scope, Model
  Geom,      // Geometry: Mesh, Points, Curves, etc.
  Light,     // Lights: RectLight, DomeLight, SphereLight, etc.
  Camera,    // Camera
  Material,  // Material, Shader, NodeGraph
  Skeleton,  // SkelRoot, Skeleton, SkelAnimation
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


// glTF-like BufferData
struct BufferData {
  ComponentType componentType{ComponentType::UInt8};
  //uint8_t count{1};           // # of components. up to 256
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<uint8_t> data;  // binary data. size is dividable by sizeof(componentType)
#else
  std::vector<uint8_t> data;  // binary data. size is dividable by sizeof(componentType)
#endif

  // TODO: Stride?
};

// Compound of ComponentType x component
enum class VertexAttributeFormat {
  Bool,     // bool(1 byte)
  Char,     // int8
  Char2,    // int8x2
  Char3,    // int8x3
  Char4,    // int8x4
  Byte,     // uint8
  Byte2,    // uint8x2
  Byte3,    // uint8x3
  Byte4,    // uint8x4
  Short,    // int16
  Short2,   // int16x2
  Short3,   // int16x2
  Short4,   // int16x2
  Ushort,   // uint16
  Ushort2,  // uint16x2
  Ushort3,  // uint16x2
  Ushort4,  // uint16x2
  Half,     // half
  Half2,    // half2
  Half3,    // half3
  Half4,    // half4
  Float,    // float
  Vec2,     // float2
  Vec3,     // float3
  Vec4,     // float4
  Int,      // int
  Ivec2,    // int2
  Ivec3,    // int3
  Ivec4,    // int4
  Uint,     // uint
  Uvec2,    // uint2
  Uvec3,    // uint3
  Uvec4,    // uint4
  Double,   // double
  Dvec2,    // double2
  Dvec3,    // double3
  Dvec4,    // double4
  Mat2,     // float 2x2
  Mat3,     // float 3x3
  Mat4,     // float 4x4
  Dmat2,    // double 2x2
  Dmat3,    // double 3x3
  Dmat4,    // double 4x4
};

static size_t VertexAttributeFormatSize(VertexAttributeFormat f) {
  size_t elemsize{0};

  switch (f) {
    case VertexAttributeFormat::Bool: {
      elemsize = 1;
      break;
    }
    case VertexAttributeFormat::Char: {
      elemsize = 1;
      break;
    }
    case VertexAttributeFormat::Char2: {
      elemsize = 2;
      break;
    }
    case VertexAttributeFormat::Char3: {
      elemsize = 3;
      break;
    }
    case VertexAttributeFormat::Char4: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Byte: {
      elemsize = 1;
      break;
    }
    case VertexAttributeFormat::Byte2: {
      elemsize = 2;
      break;
    }
    case VertexAttributeFormat::Byte3: {
      elemsize = 3;
      break;
    }
    case VertexAttributeFormat::Byte4: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Short: {
      elemsize = 2;
      break;
    }
    case VertexAttributeFormat::Short2: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Short3: {
      elemsize = 6;
      break;
    }
    case VertexAttributeFormat::Short4: {
      elemsize = 8;
      break;
    }
    case VertexAttributeFormat::Ushort: {
      elemsize = 2;
      break;
    }
    case VertexAttributeFormat::Ushort2: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Ushort3: {
      elemsize = 6;
      break;
    }
    case VertexAttributeFormat::Ushort4: {
      elemsize = 8;
      break;
    }
    case VertexAttributeFormat::Half: {
      elemsize = 2;
      break;
    }
    case VertexAttributeFormat::Half2: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Half3: {
      elemsize = 6;
      break;
    }
    case VertexAttributeFormat::Half4: {
      elemsize = 8;
      break;
    }
    case VertexAttributeFormat::Mat2: {
      elemsize = 4 * 4;
      break;
    }
    case VertexAttributeFormat::Mat3: {
      elemsize = 4 * 9;
      break;
    }
    case VertexAttributeFormat::Mat4: {
      elemsize = 4 * 16;
      break;
    }
    case VertexAttributeFormat::Dmat2: {
      elemsize = 8 * 4;
      break;
    }
    case VertexAttributeFormat::Dmat3: {
      elemsize = 8 * 9;
      break;
    }
    case VertexAttributeFormat::Dmat4: {
      elemsize = 8 * 16;
      break;
    }
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
    case VertexAttributeFormat::Int: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Ivec2: {
      elemsize = sizeof(int) * 2;
      break;
    }
    case VertexAttributeFormat::Ivec3: {
      elemsize = sizeof(int) * 3;
      break;
    }
    case VertexAttributeFormat::Ivec4: {
      elemsize = sizeof(int) * 4;
      break;
    }
    case VertexAttributeFormat::Uint: {
      elemsize = 4;
      break;
    }
    case VertexAttributeFormat::Uvec2: {
      elemsize = sizeof(uint32_t) * 2;
      break;
    }
    case VertexAttributeFormat::Uvec3: {
      elemsize = sizeof(uint32_t) * 3;
      break;
    }
    case VertexAttributeFormat::Uvec4: {
      elemsize = sizeof(uint32_t) * 4;
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

  return elemsize;
}


///
/// Vertex attribute array. Stores raw vertex attribute data.
///
/// arrayLength = elementSize * vertexCount
/// arrayBytes = formatSize * elementSize * vertexCount
///
/// Example:
///    positions(float3, elementSize=1, n=2): [1.0, 1.1, 1.2,  0.4, 0.3, 0.2]
///    skinWeights(float, elementSize=4, n=2): [1.0, 1.0, 1.0, 1.0,  0.5, 0.5,
///    0.5, 0.5]
///
struct VertexAttribute {
  std::string name;  // Attribute(primvar) name. Optional. Can be empty.
  VertexAttributeFormat format{VertexAttributeFormat::Vec3};
  uint32_t elementSize{1};  // `elementSize` in USD terminology(i.e. # of
                            // samples per vertex data)
  uint32_t stride{0};  //  We don't support packed(interleaved) vertex data, so
                       //  stride is usually sizeof(VertexAttributeFormat) *
                       //  elementSize. 0 = tightly packed.
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<uint8_t> data;  // raw binary data(TODO: Use Buffer ID?)
#else
  std::vector<uint8_t> data;  // raw binary data(TODO: Use Buffer ID?)
#endif
  std::vector<uint32_t>
      indices;  // Dedicated Index buffer. Set when variability == Indexed.
                // empty = Use externally provided vertex index buffer
  VertexVariability variability{VertexVariability::Vertex};
  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid

  //
  // Returns the number of vertex items(excludes `elementSize`).
  //
  // We use compound type for the format.
  // For example, this returns 1 when the buffer is
  // composed of 3 floats and `format` is float3(in any elementSize >= 1).
  size_t vertex_count() const {
    if (stride != 0) {
      // TODO: return 0 when (data.size() % stride) != 0?
      return data.size() / stride;
    }

    size_t itemSize = stride_bytes();

    if ((data.size() % itemSize) != 0) {
      // data size mismatch
      return 0;
    }

    return data.size() / itemSize;
  }

  inline bool empty() const {
    return data.empty();
  }

  size_t num_bytes() const { return data.size(); }

  const void *buffer() const {
    return reinterpret_cast<const void *>(data.data());
  }

  void set_buffer(const uint8_t *addr, size_t n) {
    data.resize(n);
    memcpy(data.data(), addr, n);
  }

  const std::vector<uint8_t> &get_data() const { return data; }

  std::vector<uint8_t> &get_data() { return data; }

  //
  // Bytes for each vertex item.
  // Returns `formatSize * elementSize` when `stride` is 0.
  // Returns `stride` when `stride` is not zero.
  //
  size_t stride_bytes() const {
    if (stride != 0) {
      return stride;
    }

    return element_size() * VertexAttributeFormatSize(format);
  }

  size_t element_size() const { return elementSize; }

  size_t format_size() const { return VertexAttributeFormatSize(format); }

  bool is_constant() const {
    return (variability == VertexVariability::Constant);
  }

  bool is_uniform() const {
    return (variability == VertexVariability::Uniform);
  }

  // includes 'varying'
  bool is_vertex() const {
    return (variability == VertexVariability::Vertex) ||
           (variability == VertexVariability::Varying);
  }

  bool is_facevarying() const {
    return (variability == VertexVariability::FaceVarying);
  }

  bool is_indexed() const { return variability == VertexVariability::Indexed; }
};

#if 0  // TODO: Implement
///
/// Flatten(expand by vertexCounts and vertexIndices) VertexAttribute.
///
/// @param[in] src Input VertexAttribute.
/// @param[in] faceVertexCounts Array of faceVertex counts.
/// @param[in] faceVertexIndices Array of faceVertex indices.
/// @param[out] dst flattened VertexAttribute data.
/// @param[out] itemCount # of vertex items = dst.size() / src.stride_bytes().
///
static bool FlattenVertexAttribute(
    const VertexAttribute &src,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<uint8_t> &dst,
    size_t &itemCount);
#else

#if 0  // TODO: Implement
///
/// Convert variability of `src` VertexAttribute to "facevarying".
///
/// @param[in] src Input VertexAttribute.
/// @param[in] faceVertexCounts  # of vertex per face. When the size is empty
/// and faceVertexIndices is not empty, treat `faceVertexIndices` as
/// triangulated mesh indices.
/// @param[in] faceVertexIndices
/// @param[out] dst VertexAttribute with facevarying variability. `dst.vertex_count()` become `sum(faceVertexCounts)`
///
static bool ToFacevaringVertexAttribute(
    const VertexAttribute &src, VertexAttribute &dst,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices);
#endif
#endif

//
// Convert PrimVar(type-erased value) at specified time to VertexAttribute
//
// Input Primvar's name, variability(interpolation) and elementSize are
// preserved. Use Primvar's underlying type to set the type of VertexAttribute.
// (example: 'color3f'(underlying type 'float3') -> Vec3)
//
// @param[in] pvar GeomPrimvar.
// @param[out] dst Output VertexAttribute.
// @param[out] err Error messsage. can be nullptr
// @param[in] t timecode
// @param[in] tinterp Interpolation for timesamples
//
// @return true upon success.
//
bool ToVertexAttribute(const GeomPrimvar &pvar, VertexAttribute &dst,
                       std::string *err,
                       const double t = value::TimeCode::Default(),
                       const value::TimeSampleInterpolationType tinterp =
                           value::TimeSampleInterpolationType::Linear);

enum class ColorSpace {
  sRGB,
  Lin_sRGB,     // Linear sRGB(D65)
  Rec709,
  Lin_Rec709,   // Linear Rec.709 - same primaries as sRGB but linear (MaterialX: lin_rec709)
  g22_Rec709,   // Gamma 2.2 Rec.709 (MaterialX: g22_rec709)
  g18_Rec709,   // Gamma 1.8 Rec.709 (MaterialX: g18_rec709)
  sRGB_Texture, // sRGB for textures (MaterialX: srgb_texture)
  Raw,          // Raw(physical quantity) value(e.g. normal maps, ao maps)
  Lin_ACEScg,   // ACES CG colorspace(AP1. D50)
  ACES2065_1,   // ACES 2065-1 (AP0. D60)
  Lin_Rec2020,  // Linear Rec.2020/Rec.2100
  OCIO,
  Lin_DisplayP3,   // colorSpace 'lin_displayp3'
  sRGB_DisplayP3,  // colorSpace 'srgb_displayp3'
  Custom,          // TODO: Custom colorspace
  Unknown,         // Unknown color space. 
};


// Infer colorspace from token value.
bool InferColorSpace(const value::token &tok, ColorSpace *result);

struct TextureImage {
  std::string asset_identifier;  // (resolved) filename or asset identifier.

  ComponentType texelComponentType{
      ComponentType::UInt8};  // texel bit depth of `buffer_id`
  ComponentType assetTexelComponentType{
      ComponentType::UInt8};  // texel bit depth of UsdUVTexture asset

  ColorSpace colorSpace{ColorSpace::sRGB};  // color space of texel data.
  ColorSpace usdColorSpace{
      ColorSpace::sRGB};  // original color space info in UsdUVTexture(asset meta or sourceColorSpace attrib)

  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1};  // e.g. 3 for RGB.
  int32_t miplevel{0};

  int64_t buffer_id{-1};  // index to buffer_id(texel data)

  bool decoded{false}; // true if texture data(buffer_id) is decoded. false if buffer_id contains raw image data(e.g. JPEG data)
  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

struct Cubemap
{
  // face id mapping(based on OpenGL)
  // https://www.khronos.org/opengl/wiki/Cubemap_Texture
  //
  // 0: +X (right)
  // 1: -X (left)
  // 2: +Y (top)
  // 3: -Y (bottom)
  // 4: +Z (back)
  // 5: -Z (front)

  // LoD of cubemap
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<std::array<TextureImage, 6>> faces_lod;
#else
  std::vector<std::array<TextureImage, 6>> faces_lod;
#endif
};

// Envmap lightsource
struct EnvmapLight
{
  enum class Coordinate {
    LatLong,  // "latlong"
    Angular,  // "angular"
    // MirroredBall, // TODO: "mirroredBall"
    Cubemap,  // TinyUSDZ Tydra specific.
  };

  std::string element_name;
  std::string abs_path;
  std::string display_name;

  double guideRadius{1.0e5};
  std::string asset_name; // 'inputs:texture:file'

#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<TextureImage> texture_lod;
#else
  std::vector<TextureImage> texture_lod;
#endif

  // Utility
  bool to_cubemap(Cubemap &cubemap);

};

// glTF-lie animation data

// TOOD: Implement Animation sample resampler.

// In USD, timeSamples are linearly interpolated by default.
template <typename T>
struct AnimationSampler {
  nonstd::optional<T> static_value; // value at static time('default' time) if exist
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<AnimationSample<T>> samples;
#else
  std::vector<AnimationSample<T>> samples;
#endif

  // No cubicSpline in USD
  enum class Interpolation {
    Linear,
    Step,  // Held in USD
  };

  Interpolation interpolation{Interpolation::Linear};
};

///
/// Animation interpolation mode (matches glTF specification)
///
enum class AnimationInterpolation {
  Linear,      ///< LINEAR - linear interpolation (slerp for quaternions)
  Step,        ///< STEP - discrete/stepped interpolation (no interpolation)
  CubicSpline  ///< CUBICSPLINE - cubic spline with in/out tangents
};

///
/// Animation channel target type - distinguishes what the channel animates
///
/// USD has two distinct animation systems:
/// - Node animations: xformOp time samples animate scene node transforms
/// - Skeletal animations: SkelAnimation arrays animate skeleton joint transforms
///
/// This enum enables type-safe handling of both animation types in a unified structure.
///
enum class ChannelTargetType {
  SceneNode,      ///< Targets a scene node's transform (from USD xformOp animations)
  SkeletonJoint   ///< Targets a skeleton joint (from USD SkelAnimation)
};

///
/// Animation target property path (matches glTF animation paths)
///
enum class AnimationPath {
  Translation,  ///< Animates position (vec3) - maps to .position in Three.js
  Rotation,     ///< Animates rotation (quat) - maps to .quaternion in Three.js
  Scale,        ///< Animates scale (vec3) - maps to .scale in Three.js
  Weights       ///< Animates morph target weights (float array)
};

///
/// Keyframe sampler - stores keyframe times and values in flat arrays
///
/// Matches glTF Animation Sampler structure and Three.js KeyframeTrack format.
/// Values are stored as flat float arrays:
/// - Translation/Scale: [x0,y0,z0, x1,y1,z1, ...] (3 floats per keyframe)
/// - Rotation: [x0,y0,z0,w0, x1,y1,z1,w1, ...] (4 floats per keyframe)
/// - Weights: [w0, w1, w2, ...] (1 float per keyframe per target)
///
/// For CubicSpline interpolation, each keyframe requires 3 values:
/// [in_tangent, value, out_tangent], so array size is times.size() * components * 3
///
struct KeyframeSampler {
  std::vector<float> times;   ///< Keyframe times in seconds (flat array)
  std::vector<float> values;  ///< Keyframe values (flat array)
  AnimationInterpolation interpolation{AnimationInterpolation::Linear};

  /// Check if sampler is empty
  bool empty() const { return times.empty(); }

  /// Get number of keyframes
  size_t num_keyframes() const { return times.size(); }
};

///
/// Animation channel - binds sampler data to a specific target property
///
/// Supports both node transform animations (from USD xformOps) and
/// skeletal joint animations (from USD SkelAnimation). The target_type field
/// determines how to interpret the target identification fields.
///
/// Matches glTF Animation Channel structure. Each channel targets one property
/// of one target, and references a sampler that provides the keyframe data.
///
/// Example usage:
/// ```
/// // Node animation (xformOp)
/// AnimationChannel node_channel;
/// node_channel.target_type = ChannelTargetType::SceneNode;
/// node_channel.path = AnimationPath::Translation;
/// node_channel.target_node = 5;  // Index into RenderScene::nodes
/// node_channel.sampler = 0;
///
/// // Skeletal animation (SkelAnimation)
/// AnimationChannel joint_channel;
/// joint_channel.target_type = ChannelTargetType::SkeletonJoint;
/// joint_channel.path = AnimationPath::Rotation;
/// joint_channel.skeleton_id = 0;  // Index into RenderScene::skeletons
/// joint_channel.joint_id = 12;     // Joint index within skeleton
/// joint_channel.sampler = 1;
/// ```
///
struct AnimationChannel {
  AnimationPath path;         ///< Which property to animate (translation/rotation/scale/weights)
  ChannelTargetType target_type{ChannelTargetType::SceneNode};  ///< Target type (node or joint)

  // Target identification (interpretation depends on target_type)
  int32_t target_node{-1};    ///< SceneNode: index into RenderScene::nodes (-1 = invalid)
                               ///< SkeletonJoint: unused (use skeleton_id + joint_id instead)

  // Skeletal animation fields (only used when target_type == SkeletonJoint)
  int32_t skeleton_id{-1};    ///< Index into RenderScene::skeletons (-1 = invalid)
  int32_t joint_id{-1};       ///< Index within skeleton's joint array (-1 = invalid)

  int32_t sampler{-1};        ///< Index into AnimationClip::samplers (-1 = invalid)

  /// Check if channel is valid based on its target type
  bool is_valid() const {
    if (sampler < 0) return false;
    if (target_type == ChannelTargetType::SceneNode) {
      return target_node >= 0;
    } else {  // SkeletonJoint
      return skeleton_id >= 0 && joint_id >= 0;
    }
  }

  /// Check if this is a skeletal animation channel
  bool is_skeletal() const {
    return target_type == ChannelTargetType::SkeletonJoint;
  }
};

///
/// Animation clip - collection of animation channels and samplers
///
/// Matches glTF Animation structure and Three.js AnimationClip.
/// An animation clip contains:
/// - samplers: Keyframe data (times and values)
/// - channels: Bindings from samplers to node properties
///
/// This design separates animation data from the scene hierarchy,
/// making it compatible with Three.js/glTF animation systems.
///
/// Example usage:
/// ```
/// AnimationClip clip;
/// clip.name = "Walk";
/// clip.duration = 2.0f;
///
/// // Create sampler for translation
/// KeyframeSampler trans_sampler;
/// trans_sampler.times = {0.0f, 1.0f, 2.0f};
/// trans_sampler.values = {0,0,0,  1,0,0,  0,0,0};  // Flat array: x,y,z for each keyframe
/// clip.samplers.push_back(trans_sampler);
///
/// // Create channel targeting node 5's translation
/// AnimationChannel channel;
/// channel.path = AnimationPath::Translation;
/// channel.target_node = 5;
/// channel.sampler = 0;  // Index into clip.samplers
/// clip.channels.push_back(channel);
/// ```
///
struct AnimationClip {
  std::string name;               ///< Animation name
  std::string prim_name;          ///< Original USD prim name (element name)
  std::string abs_path;           ///< Original USD absolute prim path
  std::string display_name;       ///< USD `displayName` prim meta

  float duration{0.0f};           ///< Animation duration in seconds

  std::vector<KeyframeSampler> samplers;  ///< Keyframe data
  std::vector<AnimationChannel> channels;  ///< Property bindings

  /// Check if animation is empty
  bool empty() const { return channels.empty(); }

  /// Get number of channels
  size_t num_channels() const { return channels.size(); }

  /// Check if animation contains skeletal animation channels
  bool has_skeletal_animation() const {
    for (const auto& ch : channels) {
      if (ch.is_skeletal()) return true;
    }
    return false;
  }

  /// Check if animation contains scene node animation channels
  bool has_node_animation() const {
    for (const auto& ch : channels) {
      if (!ch.is_skeletal()) return true;
    }
    return false;
  }
};

struct Node {
  std::string prim_name;     // Prim name(element name)
  std::string abs_path;      // Absolute prim path
  std::string display_name;  // `displayName` prim meta

  NodeCategory category{NodeCategory::Group};  // High-level category (Group, Geom, Light, Camera, etc.)
  NodeType nodeType{NodeType::Xform};  // Specific type within the category

  int32_t id{-1};  // Index to node content(e.g. meshes[id] when nodeTypes ==
                   // Mesh). -1 = no corresponding content exists for this node.

#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<Node> children;
#else
  std::vector<Node> children;
#endif

  // Every node have its transform at specified timecode.
  // `resetXform` is encoded in global matrix.
  value::matrix4d local_matrix;
  value::matrix4d global_matrix;  // = local_matrix * parent_matrix (USD use
                                  // row-major(pre-multiply))

  bool has_resetXform{false}; // true: When updating the transform of the node, need to reset parent's matrix to compute global matrix.

  bool is_identity_matrix() { return is_identity(local_matrix); }

  // NOTE: Animation data has been moved to RenderScene::animations (AnimationClip).
  // Animations reference nodes by index (AnimationChannel::target_node) rather than
  // being embedded in the Node structure. This matches glTF/Three.js design and
  // allows animations to be managed independently of the scene hierarchy.

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
};

// BlendShape shape target.

struct InbetweenShapeTarget {
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<vec3> pointOffsets;
  ChunkedVectorArray<vec3> normalOffsets;
#else
  std::vector<vec3> pointOffsets;
  std::vector<vec3> normalOffsets;
#endif
  float weight{0.5f};  // TODO: Init with invalid weight?
};

struct ShapeTarget {
  std::string prim_name;     // Prim name
  std::string abs_path;      // Absolute prim path
  std::string display_name;  // `displayName` prim meta

  std::vector<uint32_t> pointIndices; // Keep int array as std::vector
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<vec3> pointOffsets;
  ChunkedVectorArray<vec3> normalOffsets;
#else
  std::vector<vec3> pointOffsets;
  std::vector<vec3> normalOffsets;
#endif

  // key = weight
  std::unordered_map<float, InbetweenShapeTarget> inbetweens;
};

struct JointAndWeight {
  value::matrix4d geomBindTransform{
      value::matrix4d::identity()};  // matrix4d primvars:skel:geomBindTransform
  bool hasGeomBindTransform{false};  // true if geomBindTransform was explicitly authored in USD

  //
  // NOTE: variability of jointIndices and jointWeights are 'vertex'
  // NOTE: Values in jointIndices and jointWeights will be reordered when `MeshConverterConfig::build_vertex_indices` is set true.
  //
  std::vector<int> jointIndices;  // int[] primvars:skel:jointIndices - Keep int array as std::vector

  // NOTE: weight is converted from USD as-is. not normalized.
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<float> jointWeights;  // float[] primvars:skel:jointWeight;
#else
  std::vector<float> jointWeights;  // float[] primvars:skel:jointWeight;
#endif

  int elementSize{1}; // # of weights per vertex
};

struct MaterialPath {
  std::string material_path;           // USD Material Prim path.
  std::string backface_material_path;  // USD Material Prim path.

  // Default RenderMaterial Id to assign when
  // material_path/backface_material_path is empty. -1 = no material will be
  // assigned.
  int default_material_id{-1};
  int default_backface_material_id{-1};

  // primvar name used for texcoords when default RenderMaterial is used.
  // Currently we don't support different texcoord for each frontface and
  // backface material.
  std::string default_texcoords_primvar_name{"st"};
};

// GeomSubset whose familyName is 'materialBind'.
// For per-face material mapping.
struct MaterialSubset {
  std::string prim_name;     // Prim name in Stage
  std::string abs_path;      // Absolute Prim path in Stage
  std::string display_name;  // `displayName` Prim meta
  int64_t prim_index{-1};    // Prim index in Stage

  // Index to RenderScene::materials
  int material_id{-1};
  int backface_material_id{-1};

  // USD GeomSubset.indices. Index to a facet, i.e. index to GeomMesh.faceVertexCounts[], in USD GeomSubset
  std::vector<int> usdIndices;

  // Triangulated indices. Filled when `MeshConverterConfig::triangualte` is true
  std::vector<int> triangulatedIndices;

  const std::vector<int> &indices() const {
    return triangulatedIndices.size() ? triangulatedIndices : usdIndices;
  }

};

// Currently normals and texcoords are converted as facevarying attribute.
struct RenderMesh {
#if 0 // deprecated.
  //
  // Type of Vertex attributes of this mesh.
  //
  // `Indexed` preferred. `Facevarying` as the last resport.
  //
  enum class VertexArrayType {
    Indexed,  // 'vertex'-varying. i.e, use faceVertexIndices to draw mesh. All
              // vertex attributes must be representatable by single
              // indices(i.e, no `facevertex`-varying attribute)
    Facevarying,  // 'facevertx'-varying. When any of mesh attribute has
                  // 'facevertex' varying, we cannot represent the mesh with
                  // single indices, so decompose all vertex attribute to
                  // Facevaring(no VertexArray indices). This would impact
                  // rendering performance.
  };
#endif

  std::string prim_name;     // Prim name
  std::string abs_path;      // Absolute Prim path in Stage
  std::string display_name;  // `displayName` Prim metadataum

  // true: all vertex attributes are 'vertex'-varying. i.e, an App can simply use faceVertexIndices to draw mesh.
  // false: some vertex attributes are 'facevarying'-varying. An app need to decompose 'points' and 'vertex'-varying attribute to 'facevarying' variability to draw a mesh.
  bool is_single_indexable{false};

  //VertexArrayType vertexArrayType{VertexArrayType::Facevarying};

#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<vec3> points;  // varying is always 'vertex'.
#else
  std::vector<vec3> points;  // varying is always 'vertex'.
#endif

  ///
  /// Initialized with USD faceVertexIndices/faceVertexCounts in GeomMesh.
  /// When the mesh is triangulated, these attribute does not change.
  ///
  /// But will be modified when `MeshConverterCondig::build_vertex_indices` is set to true
  /// (To make vertex attributes of the mesh single-indexable)
  ///
  ///
  std::vector<uint32_t> usdFaceVertexIndices;
  std::vector<uint32_t> usdFaceVertexCounts;

  ///
  /// Triangulated faceVertexIndices, faceVerteCounts and auxiality state
  /// required to triangulate primvars in the app.
  ///
  /// trinangulated*** variables will be empty when the mesh is not
  /// triangulated.
  ///
  /// Topology could be changed(modified) when `MeshConverterCondig::build_vertex_indices` is set to true.
  ///
  std::vector<uint32_t> triangulatedFaceVertexIndices;
  std::vector<uint32_t> triangulatedFaceVertexCounts;

  std::vector<size_t>
      triangulatedToOrigFaceVertexIndexMap;  // used for rearrange facevertex
                                             // attrib
  std::vector<uint32_t>
      triangulatedFaceCounts;  // used for rearrange face indices(e.g GeomSubset
                               // indices)

  const std::vector<uint32_t> &faceVertexIndices() const {
    return is_triangulated() ? triangulatedFaceVertexIndices : usdFaceVertexIndices;
  }

  const std::vector<uint32_t> &faceVertexCounts() const {
    return is_triangulated() ? triangulatedFaceVertexCounts : usdFaceVertexCounts;
  }

  bool is_triangulated() const {
    return triangulatedFaceVertexIndices.size() && triangulatedFaceVertexCounts.size();
  }

  // `normals` or `primvar:normals`. Empty when no normals exist in the
  // GeomMesh.
  VertexAttribute normals;

  // key = slot ID. Usually 0 = primary
  std::unordered_map<uint32_t, VertexAttribute> texcoords;
  StringAndIdMap texcoordSlotIdMap;  // st primvarname to slotID map

  //
  // tangents and binormals(single-frame only)
  //
  // When `normals`(or `normals` primvar) is not present in the GeomMesh,
  // tangents and normals are not computed.
  //
  // When `normals` is supplied, but neither `tangents` nor `binormals` are
  // supplied in primvars, Tydra computes it based on:
  // https://learnopengl.com/Advanced-Lighting/Normal-Mapping (when
  // MeshConverterConfig::compute_tangents_and_binormals is set to `true`)
  //
  // For UsdPreviewSurface, geom primvar name of `tangents` and `binormals` are
  // read from Material's inputs::frame:tangentsPrimvarName(default "tangents"),
  // inputs::frame::binormalsPrimvarName(default "binormals")
  // https://learnopengl.com/Advanced-Lighting/Normal-Mapping
  //
  VertexAttribute tangents;
  VertexAttribute binormals;

  bool doubleSided{false};  // false = backface-cull.
  value::color3f displayColor{
      0.18f, 0.18f,
      0.18f};  // displayColor primvar(The number of array elements = 1) in USD.
               // default is set to the same in UsdPreviewSurface::diffuseColor
  float displayOpacity{
      1.0};  // displayOpacity primvar(The number of array elements = 1) in USD
  bool is_rightHanded{true};  // orientation attribute in USD.

  VertexAttribute
      vertex_colors;  // vertex color(displayColor primvar in USD). vec3.
  VertexAttribute
      vertex_opacities;  // opacity(alpha) component of vertex
                         // color(displayOpacity primvar in USD). float

  // For vertex skinning
  JointAndWeight joint_and_weights;
  int skel_id{-1}; // index to RenderScene::skeletons

  // BlendShapes
  // key = USD BlendShape prim name.
  std::map<std::string, ShapeTarget> targets;

  // Index to RenderScene::materials
  int material_id{-1};  // Material applied to whole faces in the mesh. per-face
                        // material by GeomSubset is stored in
                        // `material_subsetMap`
  int backface_material_id{
      -1};  // Backface material. Look up `rel material:binding:<BACKFACENAME>`
            // in GeomMesh. BACKFACENAME is a user-supplied setting. Default =
            // MaterialConverterConfig::default_backface_material_purpose_name

  // Key = GeomSubset name
  std::map<std::string, MaterialSubset>
      material_subsetMap;  // GeomSubset whose famiyName is 'materialBind'

  // If you want to access user-defined primvars or custom property,
  // Plese look into corresponding Prim( stage::find_prim_at_path(abs_path) )

  //
  // Area light properties (MeshLightAPI)
  // When is_area_light = true, this mesh emits light.
  //
  // Renderer integration guide:
  //   1. Calculate effective light color: light_color * light_intensity * pow(2, light_exposure)
  //   2. Apply materialSyncMode:
  //      - "materialGlowTintsLight" (default): material.emissiveColor tints the light color
  //           finalEmission = effectiveLightColor * material.emissiveColor
  //      - "independent": material emission and light are independent
  //           finalEmission = effectiveLightColor + material.emissiveColor
  //      - "noMaterialResponse": material doesn't respond to light (only emits)
  //           finalEmission = effectiveLightColor
  //   3. If light_normalize = true, divide by surface area for energy conservation
  //
  bool is_area_light{false};  // true if MeshLightAPI is applied
  std::array<float, 3> light_color{{1.0f, 1.0f, 1.0f}};  // inputs:color (linear RGB)
  float light_intensity{1.0f};  // inputs:intensity
  float light_exposure{0.0f};  // inputs:exposure (optional, in EV)
  bool light_normalize{false};  // inputs:normalize - divide by surface area if true
  std::string light_material_sync_mode;  // inputs:materialSyncMode
                                         // "materialGlowTintsLight" (default), "independent", or "noMaterialResponse"

  // Helper: Calculate effective light color with intensity and exposure applied
  inline std::array<float, 3> get_effective_light_color() const {
    float multiplier = light_intensity * std::pow(2.0f, light_exposure);
    return {{
      light_color[0] * multiplier,
      light_color[1] * multiplier,
      light_color[2] * multiplier
    }};
  }

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
  
  ///
  /// Estimate memory usage of this RenderMesh in bytes
  ///
  size_t estimate_memory_usage() const;
};

enum class UVReaderFloatComponentType {
  COMPONENT_FLOAT,
  COMPONENT_FLOAT2,
  COMPONENT_FLOAT3,
  COMPONENT_FLOAT4,
};


// TODO: Deprecate UVReaderFloat.
// float, float2, float3 or float4 only
struct UVReaderFloat {
  UVReaderFloatComponentType componentType{
      UVReaderFloatComponentType::COMPONENT_FLOAT2};
  int64_t mesh_id{-1};   // index to RenderMesh
  int64_t coord_id{-1};  // index to RenderMesh::facevaryingTexcoords

#if 0
  // Returns interpolated UV coordinate with UV transform
  // # of components filled are equal to `componentType`.
  vec4 fetchUV(size_t faceId, float varyu, float varyv);
#endif
};

struct UVTexture {
  // NOTE: it looks no 'rgba' in UsdUvTexture
  enum class Channel { R, G, B, A, RGB, RGBA };

  std::string prim_name; // element Prim name
  std::string abs_path; // Absolute Prim path
  std::string display_name; // displayName prim metadatum

  // TextureWrap `black` in UsdUVTexture is mapped to `CLAMP_TO_BORDER`(app must
  // set border color to black) default is CLAMP_TO_EDGE and `useMetadata` wrap
  // mode is ignored.
  enum class WrapMode { CLAMP_TO_EDGE, REPEAT, MIRROR, CLAMP_TO_BORDER };

  WrapMode wrapS{WrapMode::CLAMP_TO_EDGE};
  WrapMode wrapT{WrapMode::CLAMP_TO_EDGE};

  // Do CPU texture mapping. For baking texels with transform, texturing in
  // raytracer(bake lighting), etc.
  //
  // This method accounts for `tranform` and `bias/scale`
  //
  // NOTE: for R, G, B channel, The value is replicated to output[0], output[1]
  // and output[2]. For A channel, The value is returned to output[3]
  vec4 fetch_uv(size_t faceId, float varyu, float varyv);

  // `fetch_uv` with user-specified channel. `outputChannel` is ignored.
  vec4 fetch_uv_channel(size_t faceId, float varyu, float varyv,
                        Channel channel);

  // UVW version of `fetch_uv`.
  vec4 fetch_uvw(size_t faceId, float varyu, float varyv, float varyw);
  vec4 fetch_uvw_channel(size_t faceId, float varyu, float varyv, float varyw,
                         Channel channel);

  // Connected output channel(determined by connectionPath in UsdPreviewSurface)
  Channel connectedOutputChannel{Channel::RGB};

  std::set<Channel> authoredOutputChannels; // Authored `output:***` attribute in UsdUVTexture

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

// to_string functions for UVTexture nested types
std::string to_string(UVTexture::WrapMode ty);
std::string to_string(const UVTexture::Channel channel);

struct UDIMTexture {
  enum class Channel { R, G, B, RGB, RGBA };

  std::string prim_name; // element Prim name
  std::string abs_path; // Absolute Prim path
  std::string display_name; // displayName prim metadatum

  // NOTE: for single channel(e.g. R) fetch, Only [0] will be filled for the
  // return value.
  vec4 fetch(size_t faceId, float varyu, float varyv, float varyw = 1.0f,
             Channel channel = Channel::RGB);

  // key = UDIM id(e.g. 1001)
  std::unordered_map<uint32_t, int32_t> imageTileIds;
};

// ============================================================================
// LTE SpectralAPI Support
// Spectral data structures for wavelength-dependent material properties
// See doc/lte_spectral_api.md for specification
// ============================================================================

///
/// Interpolation method for spectral data
///
enum class SpectralInterpolation {
  Linear,    ///< Piecewise linear interpolation (default)
  Held,      ///< USD Held interpolation (step function)
  Cubic,     ///< Piecewise cubic interpolation (smooth)
  Sellmeier, ///< Sellmeier equation (for IOR data only)
};

///
/// Standard illuminant presets for wavelength:emission
///
enum class IlluminantPreset {
  None,  ///< No preset, use explicit SPD values
  A,     ///< CIE Standard Illuminant A (incandescent/tungsten, 2856K)
  D50,   ///< CIE Standard Illuminant D50 (horizon daylight, 5003K)
  D65,   ///< CIE Standard Illuminant D65 (noon daylight, 6504K)
  E,     ///< CIE Standard Illuminant E (equal energy)
  F1,    ///< CIE Fluorescent Illuminant F1 (daylight fluorescent)
  F2,    ///< CIE Fluorescent Illuminant F2 (cool white fluorescent)
  F7,    ///< CIE Fluorescent Illuminant F7 (D65 simulator)
  F11,   ///< CIE Fluorescent Illuminant F11 (narrow-band cool white)
};

///
/// Wavelength unit for spectral data
///
enum class WavelengthUnit {
  Nanometers,   ///< nanometers (nm), default, range [380, 780]
  Micrometers,  ///< micrometers (um), range [0.38, 0.78]
};

///
/// Spectral data container
/// Stores (wavelength, value) pairs for wavelength-dependent properties
///
struct SpectralData {
  std::vector<vec2> samples;  ///< (wavelength, value) pairs
  SpectralInterpolation interpolation{SpectralInterpolation::Linear};
  WavelengthUnit unit{WavelengthUnit::Nanometers};

  /// Check if spectral data is present
  bool has_data() const { return !samples.empty(); }

  /// Get number of samples
  size_t size() const { return samples.size(); }

  /// Evaluate spectral value at given wavelength using interpolation
  float evaluate(float wavelength) const;

  /// Convert wavelength to nanometers (for internal processing)
  float to_nanometers(float wavelength) const {
    if (unit == WavelengthUnit::Micrometers) {
      return wavelength * 1000.0f;
    }
    return wavelength;
  }
};

///
/// Spectral IOR data with Sellmeier coefficient support
///
struct SpectralIOR {
  std::vector<vec2> samples;  ///< (wavelength, IOR) pairs or Sellmeier coefficients
  SpectralInterpolation interpolation{SpectralInterpolation::Linear};
  WavelengthUnit unit{WavelengthUnit::Nanometers};

  /// Sellmeier coefficients (B1, B2, B3, C1, C2, C3)
  /// Used when interpolation == Sellmeier
  /// Note: C1, C2, C3 are in [um^2]
  float sellmeier_B1{0.0f}, sellmeier_B2{0.0f}, sellmeier_B3{0.0f};
  float sellmeier_C1{0.0f}, sellmeier_C2{0.0f}, sellmeier_C3{0.0f};

  bool has_data() const {
    return !samples.empty() || interpolation == SpectralInterpolation::Sellmeier;
  }

  /// Evaluate IOR at given wavelength
  float evaluate(float wavelength_nm) const;
};

///
/// Spectral emission data for light sources
///
struct SpectralEmission {
  std::vector<vec2> samples;  ///< (wavelength, irradiance) pairs
  SpectralInterpolation interpolation{SpectralInterpolation::Linear};
  WavelengthUnit unit{WavelengthUnit::Nanometers};
  IlluminantPreset preset{IlluminantPreset::None};

  bool has_data() const {
    return !samples.empty() || preset != IlluminantPreset::None;
  }

  /// Evaluate emission at given wavelength
  /// Returns irradiance in W m^-2 nm^-1 (normalized to nanometers)
  float evaluate(float wavelength_nm) const;
};

// String conversion functions for spectral types
std::string to_string(SpectralInterpolation interp);
std::string to_string(IlluminantPreset preset);
std::string to_string(WavelengthUnit unit);

// ============================================================================
// End of LTE SpectralAPI Support
// ============================================================================

// workaround for GCC
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

// T or TextureId
template <typename T>
class ShaderParam {
 public:
  ShaderParam() = default;
  ShaderParam(const T &t) : value(t) { }

  bool is_texture() const { return texture_id >= 0; }

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

 //private:
  T value{};
  int32_t texture_id{-1};  // negative = invalid
};

// UsdPreviewSurface
class PreviewSurfaceShader {
 public:
  bool useSpecularWorkflow{false};

  ShaderParam<vec3> diffuseColor{{0.18f, 0.18f, 0.18f}};
  ShaderParam<vec3> emissiveColor{{0.0f, 0.0f, 0.0f}};
  ShaderParam<vec3> specularColor{{0.0f, 0.0f, 0.0f}};
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

  // LTE SpectralAPI: Optional spectral properties
  // Only exported if has_data() returns true
  nonstd::optional<SpectralData> spd_reflectance;  ///< wavelength:reflectance
  nonstd::optional<SpectralIOR> spd_ior;           ///< wavelength:ior

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid

  /// Check if material has spectral reflectance data
  bool hasSpectralReflectance() const {
    return spd_reflectance.has_value() && spd_reflectance->has_data();
  }

  /// Check if material has spectral IOR data
  bool hasSpectralIOR() const {
    return spd_ior.has_value() && spd_ior->has_data();
  }
};

// MaterialX OpenPBR Surface shader optimized for WebGL/Vulkan rendering
class OpenPBRSurfaceShader {
 public:
  // Base layer - fundamental surface properties
  ShaderParam<float> base_weight{1.0f};
  ShaderParam<vec3> base_color{{0.8f, 0.8f, 0.8f}};
  ShaderParam<float> base_roughness{0.0f};
  ShaderParam<float> base_metalness{0.0f};
  ShaderParam<float> base_diffuse_roughness{0.0f};  // Oren-Nayar diffuse roughness

  // Specular layer - dielectric reflection
  ShaderParam<float> specular_weight{1.0f};
  ShaderParam<vec3> specular_color{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> specular_roughness{0.3f};
  ShaderParam<float> specular_ior{1.5f};
  ShaderParam<float> specular_ior_level{0.5f};
  ShaderParam<float> specular_anisotropy{0.0f};
  ShaderParam<float> specular_rotation{0.0f};
  
  // Transmission - transparency and refraction
  ShaderParam<float> transmission_weight{0.0f};
  ShaderParam<vec3> transmission_color{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> transmission_depth{0.0f};
  ShaderParam<vec3> transmission_scatter{{0.0f, 0.0f, 0.0f}};
  ShaderParam<float> transmission_scatter_anisotropy{0.0f};
  ShaderParam<float> transmission_dispersion{0.0f};
  
  // Subsurface scattering
  ShaderParam<float> subsurface_weight{0.0f};
  ShaderParam<vec3> subsurface_color{{0.8f, 0.8f, 0.8f}};
  ShaderParam<float> subsurface_radius{1.0f};
  ShaderParam<vec3> subsurface_radius_scale{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> subsurface_scale{1.0f};
  ShaderParam<float> subsurface_anisotropy{0.0f};
  
  // Sheen - fabric-like reflection
  ShaderParam<float> sheen_weight{0.0f};
  ShaderParam<vec3> sheen_color{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> sheen_roughness{0.3f};

  // Fuzz - velvet/fabric-like appearance
  ShaderParam<float> fuzz_weight{0.0f};
  ShaderParam<vec3> fuzz_color{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> fuzz_roughness{0.5f};

  // Thin film - iridescence from thin film interference
  ShaderParam<float> thin_film_weight{0.0f};
  ShaderParam<float> thin_film_thickness{500.0f};  // in nanometers
  ShaderParam<float> thin_film_ior{1.5f};

  // Coat layer - clear coat over surface
  ShaderParam<float> coat_weight{0.0f};
  ShaderParam<vec3> coat_color{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> coat_roughness{0.0f};
  ShaderParam<float> coat_anisotropy{0.0f};
  ShaderParam<float> coat_rotation{0.0f};
  ShaderParam<float> coat_ior{1.5f};
  ShaderParam<vec3> coat_affect_color{{1.0f, 1.0f, 1.0f}};
  ShaderParam<float> coat_affect_roughness{0.0f};
  
  // Emission - light emission
  ShaderParam<float> emission_luminance{0.0f};
  ShaderParam<vec3> emission_color{{1.0f, 1.0f, 1.0f}};

  // Geometry modifiers
  ShaderParam<float> opacity{1.0f};  // "opacity" or "geometry_opacity" (maps to alpha in Three.js)
  ShaderParam<vec3> normal{{0.0f, 0.0f, 1.0f}};
  ShaderParam<vec3> tangent{{1.0f, 0.0f, 0.0f}};

  // Tangent rotation for anisotropic materials (in degrees)
  // Blender exports tangent rotation via ND_rotate3d_vector3 node with -90 degrees
  // This value is extracted from the MaterialX NodeGraph during conversion
  // 0.0 = no rotation, -90.0 = typical Blender anisotropic rotation
  float tangent_rotation{0.0f};

  // Normal map scale factor (from ND_normalmap_float node's scale input)
  // 1.0 = default, used for bump strength adjustment
  float normal_map_scale{1.0f};

  // Coat normal and tangent for separate coat layer normal mapping
  ShaderParam<vec3> coat_normal{{0.0f, 0.0f, 1.0f}};
  ShaderParam<vec3> coat_tangent{{1.0f, 0.0f, 0.0f}};
  float coat_tangent_rotation{0.0f};
  float coat_normal_map_scale{1.0f};

  // LTE SpectralAPI: Optional spectral properties
  // Only exported to JSON if has_data() returns true
  // MaterialX property names use "spd_" prefix (e.g., "spd_reflectance", "spd_ior")
  nonstd::optional<SpectralData> spd_reflectance;  ///< wavelength:reflectance -> spd_reflectance
  nonstd::optional<SpectralIOR> spd_ior;           ///< wavelength:ior -> spd_ior
  nonstd::optional<SpectralEmission> spd_emission; ///< wavelength:emission -> spd_emission

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid

  // MaterialX Node Graph representation as JSON
  // Stores the complete node-based shader graph for reconstruction in JS/WASM
  // Schema follows MaterialX XML structure for compatibility
  // Empty string if no node graph exists (direct parameter values only)
  std::string nodeGraphJson;

  /// Check if material has spectral reflectance data
  bool hasSpectralReflectance() const {
    return spd_reflectance.has_value() && spd_reflectance->has_data();
  }

  /// Check if material has spectral IOR data
  bool hasSpectralIOR() const {
    return spd_ior.has_value() && spd_ior->has_data();
  }

  /// Check if material has spectral emission data
  bool hasSpectralEmission() const {
    return spd_emission.has_value() && spd_emission->has_data();
  }

  /// Check if material has any spectral data
  bool hasAnySpectralData() const {
    return hasSpectralReflectance() || hasSpectralIOR() || hasSpectralEmission();
  }
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// Material + Shader
// Supports dual material representation: UsdPreviewSurface and/or MaterialX OpenPBR
struct RenderMaterial {
  std::string name;  // elementName in USD (e.g. "pbrMat")
  std::string
      abs_path;  // abosolute Prim path in USD (e.g. "/_material/scope/pbrMat")
  std::string display_name;

  // Material can have UsdPreviewSurface, OpenPBR, or both
  // Use nonstd::optional to allow either/both/none
  nonstd::optional<PreviewSurfaceShader> surfaceShader;  // UsdPreviewSurface
  nonstd::optional<OpenPBRSurfaceShader> openPBRShader;  // MaterialX OpenPBR
  
  // TODO: displacement, volume.

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid
  
  // Helper methods to check which materials are available
  bool hasUsdPreviewSurface() const { return surfaceShader.has_value(); }
  bool hasOpenPBR() const { return openPBRShader.has_value(); }
  bool hasBothMaterials() const { return hasUsdPreviewSurface() && hasOpenPBR(); }
};

// Simple Camera
//
// https://openusd.org/dev/api/class_usd_geom_camera.html
//
// NOTE: Node's matrix is used for Camera matrix
// NOTE: "Y up" coordinate, right-handed coordinate space in USD.
// NOTE: Unit uses tenths of a scene unit(i.e. [mm] by default).
//       RenderSceneConverter adjusts property value to [mm] accounting for Stage's unitsPerMeter
struct RenderCamera {

  std::string name;  // elementName in USD (e.g. "frontCamera")
  std::string
      abs_path;  // abosolute GeomCamera Prim path in USD (e.g. "/xform/camera")
  std::string display_name;

  float znear{0.1f}; // clippingRange[0]
  float zfar{1000000.0f}; // clippingRange[1]
  float verticalAspectRatio{1.0}; // vertical aspect ratio

  // for Ortho camera
  float xmag{1.0f}; // horizontal maginification
  float ymag{1.0f}; // vertical maginification

  float focalLength{50.0f}; // EFL(Effective Focal Length). [mm]
  float verticalAperture{15.2908f}; // [mm]
  float horizontalAperture{20.965f}; // [mm]

  // vertical FOV in radian
  inline float yfov() {
    return 2.0f * std::atan(0.5f * verticalAperture / focalLength);
  }

  // horizontal FOV in radian
  float xfov() {
    return 2.0f * std::atan(0.5f * horizontalAperture / focalLength);
  }

  GeomCamera::Projection projection{GeomCamera::Projection::Perspective};
  GeomCamera::StereoRole stereoRole{GeomCamera::StereoRole::Mono};

  double shutterOpen{0.0};
  double shutterClose{0.0};

};

// Light source for rendering
struct RenderLight
{
  std::string name;       // elementName in USD (e.g. "sunLight")
  std::string abs_path;   // absolute Prim path in USD (e.g. "/scene/lights/sun")
  std::string display_name;

  enum class Type {
    Point,        ///< SphereLight with small radius
    Sphere,       ///< SphereLight
    Disk,         ///< DiskLight
    Rect,         ///< RectLight
    Cylinder,     ///< CylinderLight
    Distant,      ///< DistantLight (directional)
    Dome,         ///< DomeLight (environment)
    Geometry,     ///< GeometryLight
    Portal,       ///< PortalLight
  };

  enum class DomeTextureFormat {
    Automatic,
    Latlong,
    MirroredBall,
    Angular
  };

  Type type{Type::Point};

  // Common light properties (LightAPI)
  vec3 color{1.0f, 1.0f, 1.0f};       ///< Light color (linear RGB)
  float intensity{1.0f};              ///< Light intensity multiplier
  float exposure{0.0f};               ///< Exposure value (EV)
  float diffuse{1.0f};                ///< Diffuse contribution multiplier
  float specular{1.0f};               ///< Specular contribution multiplier
  bool normalize{false};              ///< Normalize by surface area

  // Color temperature
  bool enableColorTemperature{false}; ///< Use color temperature instead of color
  float colorTemperature{6500.0f};    ///< Color temperature in Kelvin

  // Transform (world space)
  mat4 transform;                     ///< World transformation matrix
  vec3 position{0.0f, 0.0f, 0.0f};    ///< World position
  vec3 direction{0.0f, -1.0f, 0.0f};  ///< Light direction (for distant/spot)

  // Type-specific parameters
  float radius{0.5f};                 ///< Sphere/Disk radius
  float width{1.0f};                  ///< RectLight width
  float height{1.0f};                 ///< RectLight height
  float length{1.0f};                 ///< CylinderLight length
  float angle{0.53f};                 ///< DistantLight angle (degrees)
  std::string textureFile;            ///< Texture for RectLight/DomeLight

  // Shaping properties (ShapingAPI)
  float shapingConeAngle{90.0f};      ///< Cone angle (degrees)
  float shapingConeSoftness{0.0f};    ///< Cone edge softness
  float shapingFocus{0.0f};           ///< Focus adjustment
  vec3 shapingFocusTint{0.0f, 0.0f, 0.0f}; ///< Focus tint color
  std::string shapingIesFile;         ///< IES profile file path
  float shapingIesAngleScale{0.0f};   ///< IES angle scale
  bool shapingIesNormalize{false};    ///< Normalize IES profile

  // Shadow properties (ShadowAPI)
  bool shadowEnable{true};            ///< Enable shadows
  vec3 shadowColor{0.0f, 0.0f, 0.0f}; ///< Shadow color
  float shadowDistance{-1.0f};        ///< Shadow distance (-1 = infinite)
  float shadowFalloff{-1.0f};         ///< Shadow falloff (-1 = no falloff)
  float shadowFalloffGamma{1.0f};     ///< Shadow falloff gamma

  // DomeLight specific
  DomeTextureFormat domeTextureFormat{DomeTextureFormat::Automatic};
  float guideRadius{1.0e5f};          ///< Radius for visualization
  int32_t envmap_texture_id{-1};      ///< Index to textures for environment map

  // GeometryLight (mesh lights with MeshLightAPI)
  int32_t geometry_mesh_id{-1};       ///< Index to meshes array for geometry lights
  std::string material_sync_mode;     ///< MeshLightAPI materialSyncMode

  // LTE SpectralAPI: Spectral emission support
  // Only exported if has_data() returns true
  nonstd::optional<SpectralEmission> spd_emission;  ///< wavelength:emission

  uint64_t handle{0};  // Handle ID for Graphics API. 0 = invalid

  /// Check if light has spectral emission data
  bool hasSpectralEmission() const {
    return spd_emission.has_value() && spd_emission->has_data();
  }
};

struct SceneMetadata
{
  std::string copyright;
  std::string comment;

  std::string upAxis{"Y"}; // "X", "Y" or "Z"
  nonstd::optional<double> startTimeCode;
  nonstd::optional<double> endTimeCode;
  double framesPerSecond{24.0};
  double timeCodesPerSecond{24.0};
  double metersPerUnit{1.0}; // default [m]

  bool autoPlay{true};

  // If you want to lookup more thing on USD Stage Metadata, Use Stage::metas()
};

// Simple glTF-like Scene Graph
class RenderScene {
 public:
  std::string usd_filename;

  SceneMetadata meta;

  uint32_t default_root_node{0}; // index to `nodes`.

#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<Node> nodes;
  ChunkedVectorArray<TextureImage> images;
  ChunkedVectorArray<RenderMaterial> materials;
  ChunkedVectorArray<RenderCamera> cameras;
  ChunkedVectorArray<RenderLight> lights;
  ChunkedVectorArray<UVTexture> textures;
  ChunkedVectorArray<RenderMesh> meshes;
  ChunkedVectorArray<AnimationClip> animations;  ///< Animation clips (glTF/Three.js compatible)
  ChunkedVectorArray<SkelHierarchy> skeletons;
  ChunkedVectorArray<BufferData>
      buffers;  // Various data storage(e.g. texel/image data).
#else
  std::vector<Node> nodes;
  std::vector<TextureImage> images;
  std::vector<RenderMaterial> materials;
  std::vector<RenderCamera> cameras;
  std::vector<RenderLight> lights;
  std::vector<UVTexture> textures;
  std::vector<RenderMesh> meshes;
  std::vector<AnimationClip> animations;  ///< Animation clips (glTF/Three.js compatible)
  std::vector<SkelHierarchy> skeletons;
  std::vector<BufferData>
      buffers;  // Various data storage(e.g. texel/image data).
#endif

  ///
  /// Estimate total memory usage of this RenderScene in bytes
  ///
  size_t estimate_memory_usage() const;

  // Variant support (inspired by glTF KHR_materials_variants)
  // Allows runtime switching between different material/geometry/property options
  // See variant-support.hh for detailed API
  std::vector<VariantGroup> variant_groups;  // Variant definitions and metadata
  std::vector<VariantSelection>
      active_selections;  // Current active variant selections
  std::map<std::string, int32_t>
      variant_group_map;  // prim_path -> variant_groups index for fast lookup

  // Get variant manager for querying and modifying variants
  // Note: This should be populated by RenderSceneConverter
  // (Currently stored as variant_groups/active_selections/variant_group_map above)

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
    const AssetResolutionResolver &assetResolver, TextureImage *imageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err);

bool DefaultTextureImageLoaderFunction(const value::AssetPath &assetPath,
                                       const AssetInfo &assetInfo,
                                       const AssetResolutionResolver &assetResolver,
                                       TextureImage *imageOut,
                                       std::vector<uint8_t> *imageData,
                                       void *userdata, std::string *warn,
                                       std::string *err);

///
/// TODO: UDIM loder
///

struct MeshConverterConfig {
  bool triangulate{true};

  // Triangulation method for polygons with 5+ vertices
  enum class TriangulationMethod {
    Earcut,     // Use earcut algorithm (robust, handles complex polygons)
    TriangleFan // Use simple triangle fan (faster, only for convex polygons)
  };

  TriangulationMethod triangulation_method{TriangulationMethod::Earcut};

  bool validate_geomsubset{true};  // Validate GeomSubset.

  // We may want texcoord data even if the Mesh does not have bound Material.
  // But we don't know which primvar is used as a texture coordinate when no
  // Texture assigned to the mesh(no PrimVar Reader assigned to) Use
  // UsdPreviewSurface setting for it.
  //
  // https://openusd.org/release/spec_usdpreviewsurface.html#usd-sample
  //
  // Also for tangnents/binormals.
  //
  // 'primvars' namespace is omitted.
  //
  std::string default_texcoords_primvar_name{"st"};
  std::string default_texcoords1_primvar_name{
      "st1"};  // for multi texture(available from iOS 16/macOS 13)
  std::string default_tangents_primvar_name{"tangents"};
  std::string default_binormals_primvar_name{"binormals"};

  // TODO: tangents1/binormals1 for multi-frame normal mapping?

  // Upperlimit of the number of skin weights per vertex.
  // For realtime app, usually up to 64
  uint32_t max_skin_elementSize = 1024ull * 256ull;

  //
  // Bone reduction: limit the number of bone influences per vertex for GPU skinning.
  // When enabled, only the strongest N bone influences are kept and weights are renormalized.
  //
  bool enable_bone_reduction{false};

  //
  // Target number of bone influences per vertex after reduction.
  // Default is 4, which is standard for real-time GPU skinning (e.g., Three.js, Unity, Unreal).
  // Common values: 2, 4, 8
  //
  uint32_t target_bone_count{4};

  //
  // Round bone count up to standard GPU skinning values without reducing influences.
  // When enabled and enable_bone_reduction is false, the elementSize is rounded up
  // to one of: 4, 8, 16, 32, 48, 64, 80, 96, 128
  // This allows passing all bone influences while ensuring compatibility with
  // GPU skinning systems that expect specific bone counts.
  //
  bool round_bone_count{false};

  //
  // Build vertex indices when vertex attributes are converted to `faceverying`?
  // Similar vertices are merged into single vertex index.
  // (convert vertex attributes from 'facevarying' to 'vertex' variability)
  //
  // Building indices is preferred for renderers which supports single
  // index-buffer only (e.g. OpenGL/Vulkan)
  //
  bool build_vertex_indices{true};

  //
  // When true, and mesh isn't single_indexable, skip BuildIndices for faster
  // and reduced temporary memory processing. Vertex attributes are all expanded
  // to facevertex varying. This option takes precedence over build_vertex_indices
  // when the mesh cannot be single-indexed.
  //
  bool prefer_non_indexed{false};

  //
  // Compute normals if not present in the mesh.
  // The algorithm computes smoothed normal for shared vertex.
  // Normals are also computed when `compute_tangents_and_binormals` is true
  // and normals primvar is not present in the mesh.
  //
  bool compute_normals{true};

  //
  // Compute tangents and binormals for tangent space normal mapping.
  // But when primary texcoords primvar is not present, tangents and binormals are not computed.
  //
  // NOTE: The algorithm is not robust to compute tangent/binormal for quad/polygons.
  // Set `triangulate` preferred when you want let Tydra compute tangent/binormal.
  //
  // NOTE: Computing tangent frame for multi-texcoord is not supported.
  //
  bool compute_tangents_and_binormals{true};

  //
  // Allowed relative error to check if vertex data is the same.
  // Used for 'facevarying' variability to `vertex` variability conversion in
  // ConvertMesh. Only effective to floating-point vertex data.
  //
  float facevarying_to_vertex_eps = std::numeric_limits<float>::epsilon();

  // When true, free GeomMesh data after converting it to save memory usage.
  // For emscripten.
  bool lowmem{false};
};

struct MaterialConverterConfig {
  // purpose name for two-sided material mapping.
  // https://github.com/syoyo/tinyusdz/issues/120
  std::string default_backface_material_purpose_name{"back"};

  // DefaultTextureImageLoader will be used when nullptr;
  TextureImageLoaderFunction texture_image_loader_function{nullptr};
  void *texture_image_loader_function_userdata{nullptr};

  // For UsdUVTexture.
  //
  // Default configuration:
  //
  // - The converter converts 8bit texture to floating point image and texel
  // value is converted to linear space.
  // - Allow missing asset(texture) and asset load failure.
  //
  // Recommended configuration for mobile/WebGL
  //
  // - `preserve_texel_bitdepth` true
  //   - No floating-point image conversion.
  // - `linearize_color_space` true
  //   - Linearlize in CPU, and no sRGB -> Linear conversion in a shader
  //   required.

  // In the UsdUVTexture spec, 8bit texture image is converted to floating point
  // image of range `[0.0, 1.0]`. When this flag is set to false, 8bit and 16bit
  // texture image is converted to floating point image. When this flag is set
  // to true, 8bit and 16bit texture data is stored as-is to save memory.
  // Setting true is good if you want to render USD scene on mobile, WebGL, etc.
  bool preserve_texel_bitdepth{false};

  // Apply the inverse of a color space to make texture image in linear space.
  // When `preserve_texel_bitdepth` is set to true, linearization also preserse
  // texel bit depth (i.e, for 8bit sRGB image, 8bit linear-space image is
  // produced)
  bool linearize_color_space{false};

  //
  // Set scene(working space) colorspace. This space must be linear colorspace.
  // Possible choice is: Linear_sRGB(linear_srgb), Lin_ACEScg(ACEScg/AP1), Lin_DisplayP3(linear_displayp3)
  // W.I.P: Curently Lin_sRGB is only supported.
  //
  ColorSpace scene_color_space{ColorSpace::Lin_sRGB};

  // Allow asset(texture, shader, etc) path with Windows backslashes(e.g.
  // ".\textures\cat.png")? When true, convert it to forward slash('/') on
  // Posixish system(otherwise character is escaped(e.g. '\t' -> tab).
  bool allow_backslash_in_asset_path{true};

  // Allow texture load failure?
  bool allow_texture_load_failure{true};

  // Allow asset(e.g. texture file/shader file) which does not exit?
  bool allow_missing_asset{true};

};

struct RenderSceneConverterConfig {
  // Load texture image data on convert.
  // false: no actual texture file/asset access.
  // App/User must setup TextureImage manually after the conversion.
  bool load_texture_assets{true};

  //
  // Merge meshes with the same material for performant rendering.
  //
  // When enabled, meshes that share the same material and have compatible
  // properties (static transforms, no per-face materials, no skeletal animation,
  // no blend shapes) will be merged into a single mesh.
  //
  // This optimization reduces draw calls in renderers like Three.js, WebGL,
  // and other GPU-based renderers where draw call overhead is significant.
  //
  // Merge criteria:
  // - Same material_id (whole mesh material, not per-face)
  // - No material_subsetMap (per-face materials prevent merging)
  // - Static mesh (no skeletal animation: skel_id == -1)
  // - No blend shapes (targets.empty())
  // - Same global transform matrix (meshes must be in the same world space,
  //   or transforms will be baked into vertex positions)
  //
  // When `bake_transform` is true:
  // - Meshes with different transforms can be merged by baking their
  //   global transforms into vertex positions/normals
  // - This allows more aggressive merging at the cost of losing
  //   individual mesh transforms
  //
  bool merge_meshes{false};

  //
  // When merging meshes, bake global transforms into vertex data.
  // This allows merging meshes with different transforms by transforming
  // their vertices into world space.
  //
  // Only effective when merge_meshes is true.
  //
  bool merge_meshes_bake_transform{true};
};

//
// Simple packed vertex struct & comparator for dedup.
// https://github.com/huamulan/OpenGL-tutorial/blob/master/common/vboindexer.cpp
//
// Up to 2 texcoords.
// tangent and binormal is included in VertexData, considering the situation
// that tangent and binormal is supplied through user-defined primvar.
//
// TODO: Use spatial hash for robust dedup(consider floating-point eps)
// TODO: Polish interface to support arbitrary vertex configuration.
//
// When TYDRA_USE_INDEX is defined, use array indices instead of values
// to save memory. Index value of -1 (or ~0u for uint32_t) means no attribute.
//

// Forward declaration for attribute arrays
template <class PackedVert>
struct DefaultVertexInput;

// Epsilon values for floating point comparison
constexpr float kPositionEps = 1e-6f;
constexpr float kAttributeEps = 1e-3f;

// Helper functions for epsilon-based comparison
inline bool float_equal(float a, float b, float eps) {
  return std::abs(a - b) <= eps;
}

inline bool float2_equal(const value::float2& a, const value::float2& b, float eps) {
  return float_equal(a[0], b[0], eps) && float_equal(a[1], b[1], eps);
}

inline bool float3_equal(const value::float3& a, const value::float3& b, float eps) {
  return float_equal(a[0], b[0], eps) && float_equal(a[1], b[1], eps) && float_equal(a[2], b[2], eps);
}

inline int float_compare(float a, float b, float eps) {
  if (float_equal(a, b, eps)) return 0;
  return (a < b) ? -1 : 1;
}

inline int float2_compare(const value::float2& a, const value::float2& b, float eps) {
  int cmp = float_compare(a[0], b[0], eps);
  if (cmp != 0) return cmp;
  return float_compare(a[1], b[1], eps);
}

inline int float3_compare(const value::float3& a, const value::float3& b, float eps) {
  int cmp = float_compare(a[0], b[0], eps);
  if (cmp != 0) return cmp;
  cmp = float_compare(a[1], b[1], eps);
  if (cmp != 0) return cmp;
  return float_compare(a[2], b[2], eps);
}

struct DefaultPackedVertexData {
  //value::float3 position;
  uint32_t point_index;
#ifdef TYDRA_USE_INDEX
  // Use indices into attribute arrays instead of values
  // -1 (or ~0u) means no attribute
  uint32_t normal_index;
  uint32_t uv0_index;
  uint32_t uv1_index;
  uint32_t tangent_index;
  uint32_t binormal_index;
  uint32_t color_index;
  uint32_t opacity_index;
#else
  // Use values directly (original behavior)
  value::float3 normal;
  value::float2 uv0;
  value::float2 uv1;
  value::float3 tangent;
  value::float3 binormal;
  value::float3 color;
  float opacity;
#endif

  // Basic comparator for std::map (fallback to memcmp)
  // For epsilon-based comparison, use DefaultPackedVertexDataCompare with attribute arrays
  bool operator<(const DefaultPackedVertexData &rhs) const {
    return memcmp(reinterpret_cast<const void *>(this),
                  reinterpret_cast<const void *>(&rhs),
                  sizeof(DefaultPackedVertexData)) > 0;
  }
};

struct DefaultPackedVertexDataHasher {
  inline size_t operator()(const DefaultPackedVertexData &v) const {
    // Simple hasher using FNV1 32bit
    // TODO: Use 64bit FNV1?
    // TODO: Use spatial hash or LSH(LocallySensitiveHash) for position value.
    static constexpr uint32_t kFNV_Prime = 0x01000193;
    static constexpr uint32_t kFNV_Offset_Basis = 0x811c9dc5;

    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(&v);
    size_t n = sizeof(DefaultPackedVertexData);

    uint32_t hash = kFNV_Offset_Basis;
    for (size_t i = 0; i < n; i++) {
      hash = (kFNV_Prime * hash) ^ (ptr[i]);
    }

    return size_t(hash);
  }
};

struct DefaultPackedVertexDataEqual {
  bool operator()(const DefaultPackedVertexData &lhs,
                  const DefaultPackedVertexData &rhs) const {
    return memcmp(reinterpret_cast<const void *>(&lhs),
                  reinterpret_cast<const void *>(&rhs),
                  sizeof(DefaultPackedVertexData)) == 0;
  }
};

// Epsilon-based comparison with access to attribute arrays
template <class VertexInput>
struct DefaultPackedVertexDataCompare {
  const VertexInput* vertex_input;
  
  DefaultPackedVertexDataCompare(const VertexInput* input) : vertex_input(input) {}
  
  bool operator()(const DefaultPackedVertexData &lhs,
                  const DefaultPackedVertexData &rhs) const {
    // Compare point indices first
    if (lhs.point_index != rhs.point_index) {
      return lhs.point_index < rhs.point_index;
    }
    
#ifdef TYDRA_USE_INDEX
    // In index mode, resolve indices to values and compare with epsilon
    if (!vertex_input) {
      // Fallback to index comparison if no vertex input available
      return memcmp(&lhs, &rhs, sizeof(DefaultPackedVertexData)) < 0;
    }
    
    // Compare normals
    if (lhs.normal_index != rhs.normal_index) {
      if (lhs.normal_index == ~0u) return true;  // lhs has no normal, rhs has normal
      if (rhs.normal_index == ~0u) return false; // rhs has no normal, lhs has normal
      
      const auto& lhs_normal = vertex_input->unique_normals[lhs.normal_index];
      const auto& rhs_normal = vertex_input->unique_normals[rhs.normal_index];
      int cmp = float3_compare(lhs_normal, rhs_normal, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
    // Compare uv0
    if (lhs.uv0_index != rhs.uv0_index) {
      if (lhs.uv0_index == ~0u) return true;
      if (rhs.uv0_index == ~0u) return false;
      
      const auto& lhs_uv0 = vertex_input->unique_uv0s[lhs.uv0_index];
      const auto& rhs_uv0 = vertex_input->unique_uv0s[rhs.uv0_index];
      int cmp = float2_compare(lhs_uv0, rhs_uv0, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
    // Compare uv1
    if (lhs.uv1_index != rhs.uv1_index) {
      if (lhs.uv1_index == ~0u) return true;
      if (rhs.uv1_index == ~0u) return false;
      
      const auto& lhs_uv1 = vertex_input->unique_uv1s[lhs.uv1_index];
      const auto& rhs_uv1 = vertex_input->unique_uv1s[rhs.uv1_index];
      int cmp = float2_compare(lhs_uv1, rhs_uv1, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
    // Compare tangents
    if (lhs.tangent_index != rhs.tangent_index) {
      if (lhs.tangent_index == ~0u) return true;
      if (rhs.tangent_index == ~0u) return false;
      
      const auto& lhs_tangent = vertex_input->unique_tangents[lhs.tangent_index];
      const auto& rhs_tangent = vertex_input->unique_tangents[rhs.tangent_index];
      int cmp = float3_compare(lhs_tangent, rhs_tangent, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
    // Compare binormals
    if (lhs.binormal_index != rhs.binormal_index) {
      if (lhs.binormal_index == ~0u) return true;
      if (rhs.binormal_index == ~0u) return false;
      
      const auto& lhs_binormal = vertex_input->unique_binormals[lhs.binormal_index];
      const auto& rhs_binormal = vertex_input->unique_binormals[rhs.binormal_index];
      int cmp = float3_compare(lhs_binormal, rhs_binormal, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
    // Compare colors
    if (lhs.color_index != rhs.color_index) {
      if (lhs.color_index == ~0u) return true;
      if (rhs.color_index == ~0u) return false;
      
      const auto& lhs_color = vertex_input->unique_colors[lhs.color_index];
      const auto& rhs_color = vertex_input->unique_colors[rhs.color_index];
      int cmp = float3_compare(lhs_color, rhs_color, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
    // Compare opacity
    if (lhs.opacity_index != rhs.opacity_index) {
      if (lhs.opacity_index == ~0u) return true;
      if (rhs.opacity_index == ~0u) return false;
      
      const float lhs_opacity = vertex_input->unique_opacities[lhs.opacity_index];
      const float rhs_opacity = vertex_input->unique_opacities[rhs.opacity_index];
      int cmp = float_compare(lhs_opacity, rhs_opacity, kAttributeEps);
      if (cmp != 0) return cmp < 0;
    }
    
#else
    // Direct value comparison with epsilon
    int cmp = float3_compare(lhs.normal, rhs.normal, kAttributeEps);
    if (cmp != 0) return cmp < 0;
    
    cmp = float2_compare(lhs.uv0, rhs.uv0, kAttributeEps);
    if (cmp != 0) return cmp < 0;
    
    cmp = float2_compare(lhs.uv1, rhs.uv1, kAttributeEps);
    if (cmp != 0) return cmp < 0;
    
    cmp = float3_compare(lhs.tangent, rhs.tangent, kAttributeEps);
    if (cmp != 0) return cmp < 0;
    
    cmp = float3_compare(lhs.binormal, rhs.binormal, kAttributeEps);
    if (cmp != 0) return cmp < 0;
    
    cmp = float3_compare(lhs.color, rhs.color, kAttributeEps);
    if (cmp != 0) return cmp < 0;
    
    cmp = float_compare(lhs.opacity, rhs.opacity, kAttributeEps);
    if (cmp != 0) return cmp < 0;
#endif
    
    return false; // All values are equal within epsilon
  }
};

// Epsilon-based equality comparison with access to attribute arrays
template <class VertexInput>
struct DefaultPackedVertexDataEqualEps {
  const VertexInput* vertex_input;
  
  DefaultPackedVertexDataEqualEps(const VertexInput* input) : vertex_input(input) {}
  
  bool operator()(const DefaultPackedVertexData &lhs,
                  const DefaultPackedVertexData &rhs) const {
    // Compare point indices first
    if (lhs.point_index != rhs.point_index) {
      return false;
    }
    
#ifdef TYDRA_USE_INDEX
    // In index mode, resolve indices to values and compare with epsilon
    if (!vertex_input) {
      // Fallback to exact comparison if no vertex input available
      return memcmp(&lhs, &rhs, sizeof(DefaultPackedVertexData)) == 0;
    }
    
    // Compare normals
    if (lhs.normal_index != rhs.normal_index) {
      if (lhs.normal_index == ~0u || rhs.normal_index == ~0u) {
        return lhs.normal_index == rhs.normal_index; // Both must be missing
      }
      const auto& lhs_normal = vertex_input->unique_normals[lhs.normal_index];
      const auto& rhs_normal = vertex_input->unique_normals[rhs.normal_index];
      if (!float3_equal(lhs_normal, rhs_normal, kAttributeEps)) return false;
    }
    
    // Compare uv0
    if (lhs.uv0_index != rhs.uv0_index) {
      if (lhs.uv0_index == ~0u || rhs.uv0_index == ~0u) {
        return lhs.uv0_index == rhs.uv0_index;
      }
      const auto& lhs_uv0 = vertex_input->unique_uv0s[lhs.uv0_index];
      const auto& rhs_uv0 = vertex_input->unique_uv0s[rhs.uv0_index];
      if (!float2_equal(lhs_uv0, rhs_uv0, kAttributeEps)) return false;
    }
    
    // Compare uv1
    if (lhs.uv1_index != rhs.uv1_index) {
      if (lhs.uv1_index == ~0u || rhs.uv1_index == ~0u) {
        return lhs.uv1_index == rhs.uv1_index;
      }
      const auto& lhs_uv1 = vertex_input->unique_uv1s[lhs.uv1_index];
      const auto& rhs_uv1 = vertex_input->unique_uv1s[rhs.uv1_index];
      if (!float2_equal(lhs_uv1, rhs_uv1, kAttributeEps)) return false;
    }
    
    // Compare tangents
    if (lhs.tangent_index != rhs.tangent_index) {
      if (lhs.tangent_index == ~0u || rhs.tangent_index == ~0u) {
        return lhs.tangent_index == rhs.tangent_index;
      }
      const auto& lhs_tangent = vertex_input->unique_tangents[lhs.tangent_index];
      const auto& rhs_tangent = vertex_input->unique_tangents[rhs.tangent_index];
      if (!float3_equal(lhs_tangent, rhs_tangent, kAttributeEps)) return false;
    }
    
    // Compare binormals
    if (lhs.binormal_index != rhs.binormal_index) {
      if (lhs.binormal_index == ~0u || rhs.binormal_index == ~0u) {
        return lhs.binormal_index == rhs.binormal_index;
      }
      const auto& lhs_binormal = vertex_input->unique_binormals[lhs.binormal_index];
      const auto& rhs_binormal = vertex_input->unique_binormals[rhs.binormal_index];
      if (!float3_equal(lhs_binormal, rhs_binormal, kAttributeEps)) return false;
    }
    
    // Compare colors
    if (lhs.color_index != rhs.color_index) {
      if (lhs.color_index == ~0u || rhs.color_index == ~0u) {
        return lhs.color_index == rhs.color_index;
      }
      const auto& lhs_color = vertex_input->unique_colors[lhs.color_index];
      const auto& rhs_color = vertex_input->unique_colors[rhs.color_index];
      if (!float3_equal(lhs_color, rhs_color, kAttributeEps)) return false;
    }
    
    // Compare opacity
    if (lhs.opacity_index != rhs.opacity_index) {
      if (lhs.opacity_index == ~0u || rhs.opacity_index == ~0u) {
        return lhs.opacity_index == rhs.opacity_index;
      }
      const float lhs_opacity = vertex_input->unique_opacities[lhs.opacity_index];
      const float rhs_opacity = vertex_input->unique_opacities[rhs.opacity_index];
      if (!float_equal(lhs_opacity, rhs_opacity, kAttributeEps)) return false;
    }
    
#else
    // Direct value comparison with epsilon
    if (!float3_equal(lhs.normal, rhs.normal, kAttributeEps)) return false;
    if (!float2_equal(lhs.uv0, rhs.uv0, kAttributeEps)) return false;
    if (!float2_equal(lhs.uv1, rhs.uv1, kAttributeEps)) return false;
    if (!float3_equal(lhs.tangent, rhs.tangent, kAttributeEps)) return false;
    if (!float3_equal(lhs.binormal, rhs.binormal, kAttributeEps)) return false;
    if (!float3_equal(lhs.color, rhs.color, kAttributeEps)) return false;
    if (!float_equal(lhs.opacity, rhs.opacity, kAttributeEps)) return false;
#endif
    
    return true; // All values are equal within epsilon
  }
};

template <class PackedVert>
struct DefaultVertexInput {
  //std::vector<value::float3> positions;
  std::vector<uint32_t> point_indices; // Keep int array as std::vector
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<value::float3> normals;
  ChunkedVectorArray<value::float2> uv0s;
  ChunkedVectorArray<value::float2> uv1s;
  ChunkedVectorArray<value::float3> tangents;
  ChunkedVectorArray<value::float3> binormals;
  ChunkedVectorArray<value::float3> colors;
  ChunkedVectorArray<float> opacities;
#else
  std::vector<value::float3> normals;
  std::vector<value::float2> uv0s;
  std::vector<value::float2> uv1s;
  std::vector<value::float3> tangents;
  std::vector<value::float3> binormals;
  std::vector<value::float3> colors;
  std::vector<float> opacities;
#endif

#ifdef TYDRA_USE_INDEX
  // Unique attribute arrays for indexed mode
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<value::float3> unique_normals;
  ChunkedVectorArray<value::float2> unique_uv0s;
  ChunkedVectorArray<value::float2> unique_uv1s;
  ChunkedVectorArray<value::float3> unique_tangents;
  ChunkedVectorArray<value::float3> unique_binormals;
  ChunkedVectorArray<value::float3> unique_colors;
  ChunkedVectorArray<float> unique_opacities;
#else
  std::vector<value::float3> unique_normals;
  std::vector<value::float2> unique_uv0s;
  std::vector<value::float2> unique_uv1s;
  std::vector<value::float3> unique_tangents;
  std::vector<value::float3> unique_binormals;
  std::vector<value::float3> unique_colors;
  std::vector<float> unique_opacities;
#endif
#endif

  size_t size() const { return point_indices.size(); }

  void get(size_t idx, PackedVert &output) const {
    if (idx < point_indices.size()) {
      output.point_index = point_indices[idx];
    } else {
      output.point_index = ~0u; // this case should not happen though
    }
#ifdef TYDRA_USE_INDEX
    // In index mode, store indices to unique attribute arrays
    // The indices will be set by the conversion process
    // For now, we just mark them as not present if no data
    output.normal_index = (idx < normals.size()) ? idx : ~0u;
    output.uv0_index = (idx < uv0s.size()) ? idx : ~0u;
    output.uv1_index = (idx < uv1s.size()) ? idx : ~0u;
    output.tangent_index = (idx < tangents.size()) ? idx : ~0u;
    output.binormal_index = (idx < binormals.size()) ? idx : ~0u;
    output.color_index = (idx < colors.size()) ? idx : ~0u;
    output.opacity_index = (idx < opacities.size()) ? idx : ~0u;
#else
    // Original behavior: store values directly
    if (idx < normals.size()) {
      output.normal = normals[idx];
    } else {
      output.normal = {0.0f, 0.0f, 0.0f};
    }
    if (idx < uv0s.size()) {
      output.uv0 = uv0s[idx];
    } else {
      output.uv0 = {0.0f, 0.0f};
    }
    if (idx < uv1s.size()) {
      output.uv1 = uv1s[idx];
    } else {
      output.uv1 = {0.0f, 0.0f};
    }
    if (idx < tangents.size()) {
      output.tangent = tangents[idx];
    } else {
      output.tangent = {0.0f, 0.0f, 0.0f};
    }
    if (idx < binormals.size()) {
      output.binormal = binormals[idx];
    } else {
      output.binormal = {0.0f, 0.0f, 0.0f};
    }
    if (idx < colors.size()) {
      output.color = colors[idx];
    } else {
      output.color = {0.0f, 0.0f, 0.0f};
    }
    if (idx < opacities.size()) {
      output.opacity = opacities[idx];
    } else {
      output.opacity = 0.0f;  // FIXME: Use 1.0?
    }
#endif
  }
};

template <class PackedVert>
struct DefaultVertexOutput {
  //std::vector<value::float3> positions;
  std::vector<uint32_t> point_indices; // Keep int array as std::vector
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<value::float3> normals;
  ChunkedVectorArray<value::float2> uv0s;
  ChunkedVectorArray<value::float2> uv1s;
  ChunkedVectorArray<value::float3> tangents;
  ChunkedVectorArray<value::float3> binormals;
  ChunkedVectorArray<value::float3> colors;
  ChunkedVectorArray<float> opacities;
#else
  std::vector<value::float3> normals;
  std::vector<value::float2> uv0s;
  std::vector<value::float2> uv1s;
  std::vector<value::float3> tangents;
  std::vector<value::float3> binormals;
  std::vector<value::float3> colors;
  std::vector<float> opacities;
#endif

#ifdef TYDRA_USE_INDEX
  // Unique attribute arrays for indexed mode
#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<value::float3> unique_normals;
  ChunkedVectorArray<value::float2> unique_uv0s;
  ChunkedVectorArray<value::float2> unique_uv1s;
  ChunkedVectorArray<value::float3> unique_tangents;
  ChunkedVectorArray<value::float3> unique_binormals;
  ChunkedVectorArray<value::float3> unique_colors;
  ChunkedVectorArray<float> unique_opacities;
#else
  std::vector<value::float3> unique_normals;
  std::vector<value::float2> unique_uv0s;
  std::vector<value::float2> unique_uv1s;
  std::vector<value::float3> unique_tangents;
  std::vector<value::float3> unique_binormals;
  std::vector<value::float3> unique_colors;
  std::vector<float> unique_opacities;
#endif
#endif

  size_t size() const { return point_indices.size(); }

  void push_back(const PackedVert &v) {
    point_indices.push_back(v.point_index);
#ifdef TYDRA_USE_INDEX
    // In index mode, we would resolve indices to actual values
    // from the unique arrays when needed for rendering
    // For now, we keep the existing interface but store indices
    // This would need additional logic to resolve indices to values
    normals.push_back({0.0f, 0.0f, 0.0f}); // placeholder
    uv0s.push_back({0.0f, 0.0f});
    uv1s.push_back({0.0f, 0.0f});
    tangents.push_back({0.0f, 0.0f, 0.0f});
    binormals.push_back({0.0f, 0.0f, 0.0f});
    colors.push_back({0.0f, 0.0f, 0.0f});
    opacities.push_back(0.0f);
#else
    normals.push_back(v.normal);
    uv0s.push_back(v.uv0);
    uv1s.push_back(v.uv1);
    tangents.push_back(v.tangent);
    binormals.push_back(v.binormal);
    colors.push_back(v.color);
    opacities.push_back(v.opacity);
#endif
  }
};

//
// out_vertex_indices_remap: corresponding vertexIndex in input.
//
template <class VertexInput, class VertexOutput, class PackedVert,
          class PackedVertHasher, class PackedVertEqual>
void BuildIndices(const VertexInput &input, VertexOutput &output,
                  std::vector<uint32_t> &out_indices, std::vector<uint32_t> &out_point_indices)
{
  // Original implementation using unordered_map
  // For better performance on large meshes, consider using BuildIndicesWithSpatialHash
  std::unordered_map<PackedVert, uint32_t, PackedVertHasher, PackedVertEqual>
      vertexToIndexMap;

  auto GetSimilarVertex = [&](const PackedVert &v, uint32_t &out_idx) -> bool {
    auto it = vertexToIndexMap.find(v);
    if (it == vertexToIndexMap.end()) {
      return false;
    }

    out_idx = it->second;
    return true;
  };

  for (size_t i = 0; i < input.size(); i++) {
    PackedVert v;
    input.get(i, v);

    uint32_t index{0};
    bool found = GetSimilarVertex(v, index);
    if (found) {
      out_indices.push_back(index);
    } else {
      uint32_t new_index = uint32_t(output.size());
      out_indices.push_back(new_index);
      output.push_back(v);
      vertexToIndexMap[v] = new_index;
    }
    out_point_indices.push_back(v.point_index);
  }
}

//
// BuildIndicesWithSpatialHash - Optimized version using spatial hashing
// for efficient vertex similarity search
//
// Use this for large meshes where vertex deduplication is a bottleneck.
// The spatial hash grid provides O(1) average-case lookup with better
// cache locality than the standard hash map approach.
//
template <class VertexInput, class VertexOutput, class PackedVert>
void BuildIndicesWithSpatialHash(
    const VertexInput &input, 
    VertexOutput &output,
    std::vector<uint32_t> &out_indices, 
    std::vector<uint32_t> &out_point_indices,
    float cellSize = 0.01f,        // Grid cell size for spatial hashing
    float positionEps = 1e-6f,      // Epsilon for position comparison
    float attributeEps = 1e-3f)     // Epsilon for attribute comparison
{
  using namespace spatial;
  
  // Initialize spatial hash grid
  VertexSpatialHashGrid<float> spatialGrid(cellSize, positionEps, attributeEps);
  
  // Reserve space for expected vertices
  spatialGrid.reserveVertices(input.size());
  output.reserve(input.size() / 4); // Assume roughly 25% unique vertices
  
  // Process each input vertex
  for (size_t i = 0; i < input.size(); i++) {
    PackedVert v;
    input.get(i, v);
    
    // Convert PackedVert to spatial hash vertex format
    typename VertexSpatialHashGrid<float>::Vertex spatialVertex;
    
    // Get position from points array if available
    if (v.point_index < input.point_indices.size()) {
      // Note: This assumes points are available elsewhere in the context
      // For now, we'll use the point_index as a placeholder
      spatialVertex.position = {0, 0, 0}; // Would be filled from actual points
    }
    
#ifdef TYDRA_USE_INDEX
    // Resolve indices to values for spatial search
    if (v.normal_index != ~0u && v.normal_index < input.unique_normals.size()) {
      spatialVertex.normal = input.unique_normals[v.normal_index];
    }
    if (v.uv0_index != ~0u && v.uv0_index < input.unique_uv0s.size()) {
      spatialVertex.uv0 = input.unique_uv0s[v.uv0_index];
    }
    if (v.uv1_index != ~0u && v.uv1_index < input.unique_uv1s.size()) {
      spatialVertex.uv1 = input.unique_uv1s[v.uv1_index];
    }
    if (v.tangent_index != ~0u && v.tangent_index < input.unique_tangents.size()) {
      spatialVertex.tangent = input.unique_tangents[v.tangent_index];
    }
    if (v.binormal_index != ~0u && v.binormal_index < input.unique_binormals.size()) {
      spatialVertex.binormal = input.unique_binormals[v.binormal_index];
    }
    if (v.color_index != ~0u && v.color_index < input.unique_colors.size()) {
      spatialVertex.color = input.unique_colors[v.color_index];
    }
    if (v.opacity_index != ~0u && v.opacity_index < input.unique_opacities.size()) {
      spatialVertex.opacity = input.unique_opacities[v.opacity_index];
    }
#else
    // Direct value access
    spatialVertex.normal = v.normal;
    spatialVertex.uv0 = v.uv0;
    spatialVertex.uv1 = v.uv1;
    spatialVertex.tangent = v.tangent;
    spatialVertex.binormal = v.binormal;
    spatialVertex.color = v.color;
    spatialVertex.opacity = v.opacity;
#endif
    
    spatialVertex.id = static_cast<uint32_t>(i);
    
    // Check if similar vertex exists
    uint32_t existingId;
    bool found = spatialGrid.findExactVertex(spatialVertex, existingId);
    
    if (found && existingId < output.size()) {
      // Use existing vertex
      out_indices.push_back(existingId);
    } else {
      // Add new unique vertex
      uint32_t new_index = static_cast<uint32_t>(output.size());
      out_indices.push_back(new_index);
      output.push_back(v);
      
      // Update spatial vertex id to match output index
      spatialVertex.id = new_index;
      spatialGrid.addVertex(spatialVertex);
    }
    
    out_point_indices.push_back(v.point_index);
  }
  
  // Build spatial grid after all vertices are added
  spatialGrid.build();
  
  // Optional: Get statistics for debugging
  if (false) { // Set to true for debugging
    size_t totalCells, maxCellSize, avgCellSize, subdivisions;
    spatialGrid.getStatistics(totalCells, maxCellSize, avgCellSize, subdivisions);
    // Log statistics...
  }
}

class RenderSceneConverterEnv {
 public:
  RenderSceneConverterEnv(const Stage &_stage) : stage(_stage) {}

  RenderSceneConverterConfig scene_config;
  MeshConverterConfig mesh_config;
  MaterialConverterConfig material_config;

  AssetResolutionResolver asset_resolver;

  std::string usd_filename; // Corresponding USD filename to Stage.

  void set_search_paths(const std::vector<std::string> &paths) {
    asset_resolver.set_search_paths(paths);
  }

  const Stage &stage;  // Point to valid Stage object at constructor

  double timecode{value::TimeCode::Default()};
  value::TimeSampleInterpolationType tinterp{
      value::TimeSampleInterpolationType::Linear};

};

//
// Convert USD scenegraph at specified time
// TODO: Use RenderSceneConverterEnv(RenderSceneConverterEnv::timecode)
//
class RenderSceneConverter {
 public:
  RenderSceneConverter() = default;
  RenderSceneConverter(const RenderSceneConverter &rhs) = delete;
  RenderSceneConverter(RenderSceneConverter &&rhs) = delete;

  ///
  /// Set progress callback for monitoring conversion progress.
  ///
  /// @param[in] callback Function to call during conversion to report progress
  /// @param[in] userptr User-provided pointer for custom data
  ///
  void SetProgressCallback(ProgressCallback callback, void *userptr = nullptr);

  ///
  /// Set detailed progress callback for fine-grained progress monitoring.
  /// This callback provides mesh/material/texture counts during conversion.
  ///
  /// @param[in] callback Function to call during conversion with detailed info
  /// @param[in] userptr User-provided pointer for custom data
  ///
  void SetDetailedProgressCallback(DetailedProgressCallback callback, void *userptr = nullptr);

  ///
  /// Report mesh conversion progress (for use by MeshVisitor).
  /// Updates internal progress info and calls the detailed callback if set.
  ///
  /// @param[in] meshes_processed Number of meshes processed so far
  /// @param[in] meshes_total Total number of meshes to process
  /// @param[in] mesh_name Name of the current mesh being processed
  /// @param[in] message Progress message
  /// @return true to continue, false to cancel
  ///
  bool ReportMeshProgress(size_t meshes_processed, size_t meshes_total,
                          const std::string& mesh_name, const std::string& message);

  ///
  /// All-in-one Stage to RenderScene conversion.
  ///
  /// Convert Stage to RenderScene.
  /// Must be called after SetStage, SetMaterialConverterConfig(optional)
  ///
  bool ConvertToRenderScene(const RenderSceneConverterEnv &env, RenderScene *scene);

  const std::string &GetInfo() const { return _info; }
  const std::string &GetWarning() const { return _warn; }
  const std::string &GetError() const { return _err; }

  // Prim path <-> index for corresponding array
  // e.g. meshMap: primPath/index to `meshes`.

  // TODO: Move to private?
  StringAndIdMap root_nodeMap;
  StringAndIdMap meshMap;
  StringAndIdMap materialMap;
  StringAndIdMap cameraMap;
  StringAndIdMap lightMap;
  StringAndIdMap textureMap;
  StringAndIdMap imageMap;
  StringAndIdMap bufferMap;
  StringAndIdMap animationMap;

  int default_node{-1};

#ifdef TYDRA_USE_CHUNKED_ARRAY
  ChunkedVectorArray<Node> root_nodes;
  ChunkedVectorArray<RenderMesh> meshes;
  ChunkedVectorArray<RenderMaterial> materials;
  ChunkedVectorArray<RenderCamera> cameras;
  ChunkedVectorArray<RenderLight> lights;
  ChunkedVectorArray<UVTexture> textures;
  ChunkedVectorArray<TextureImage> images;
  ChunkedVectorArray<BufferData> buffers;
  ChunkedVectorArray<SkelHierarchy> skeletons;
  ChunkedVectorArray<AnimationClip> animations;
#else
  std::vector<Node> root_nodes;
  std::vector<RenderMesh> meshes;
  std::vector<RenderMaterial> materials;
  std::vector<RenderCamera> cameras;
  std::vector<RenderLight> lights;
  std::vector<UVTexture> textures;
  std::vector<TextureImage> images;
  std::vector<BufferData> buffers;
  std::vector<SkelHierarchy> skeletons;
  std::vector<AnimationClip> animations;
#endif

  // Pre-discovered skeleton/animation prims for ancestor-based discovery
  // These are set temporarily during ConvertToRenderScene
  const PathPrimMap<Skeleton> *_allSkeletons{nullptr};
  const PathPrimMap<SkelRoot> *_allSkelRoots{nullptr};
  const PathPrimMap<SkelAnimation> *_allAnimations{nullptr};

  ///
  /// Convert GeomMesh to renderer-friendly mesh.
  /// Also apply triangulation when MeshConverterConfig::triangulate is set to
  /// true.
  ///
  /// normals, texcoords, vertexcolors/opacities vertex attributes(built-in
  /// primvars) are converterd to either `vertex` variability(i.e. can be drawn
  /// with single vertex indices) or `facevarying` variability(any of primvars
  /// is `facevarying`. It can be drawn with no indices, but less
  /// efficient(especially vertex has skin weights and blendshapes)).
  ///
  /// Since preferred variability for OpenGL/Vulkan renderer is `vertex`,
  /// ConvertMesh tries to convert `facevarying` attribute to `vertex` attribute
  /// when all shared vertex data is the same. If it fails, but
  /// `MeshConverterConfig.build_indices` is set to true, ConvertMesh builds
  /// vertex indices from `facevarying` and convert variability to 'vertex'.
  ///
  /// Note that `points`, skin weights and BlendShape attributes are remains
  /// with `vertex` variability. (so that we can apply some processing per
  /// point-wise)
  ///
  /// Thus, if you want to render a mesh whose normal/texcoord/etc variability
  /// is `facevarying`, `points`, skin weights and BlendShape attributes would
  /// also need to be converted to `facevarying` to draw.
  ///
  /// Other user defined primvars are not touched by ConvertMesh.
  /// The app need to manually triangulate, change variability of user-defined
  /// primvar if required.
  ///
  /// It is recommended first convert Materials assigned(bounded) to this
  /// GeomMesh(and GeomSubsets) or create your own Materials, and supply
  /// material info with `material_path` and `rmaterial_map`. You may supply
  /// empty material info and assign Material after ConvertMesh manually, but it
  /// will need some steps(Need to find texcoord primvar, triangulate texcoord,
  /// etc). See the implementation of ConvertMesh for details)
  ///
  ///
  /// @param[in] mesh_abs_path USD prim path to this GeomMesh
  /// @param[in] mesh Input GeomMesh
  /// @param[in] material_path USD Material Prim path assigned(bound) to this
  /// GeomMesh. Use tydra::GetBoundPath to get Material path actually assigned
  /// to the mesh.
  /// @param[in] subset_material_path_map USD Material Prim path assigned(bound)
  /// to GeomSubsets in this GeomMesh. key = GeomSubset Prim name.
  /// @param[in] rmaterial_map USD Material Prim path <-> RenderMaterial index
  /// map. Use empty map if no material assigned to this Mesh. If the mesh has
  /// bounded material(including material from GeomSubset), RenderMaterial index
  /// must be obrained using ConvertMaterial method before calling ConvertMesh.
  /// @param[in] material_subsets GeomSubset assigned to this Mesh. Can be empty
  /// when no materialBind GeomSuset assigned to this mesh.
  /// @param[in] blendshapes BlendShape Prims assigned to this Mesh. Can be
  /// empty when no BlendShape assigned to this mesh.
  /// @param[out] dst RenderMesh output
  ///
  /// @return true when success.
  ///
  ///

  ///
  /// Convert GeomCube to RenderMesh by generating tessellated geometry
  ///
  /// @param[in] env Converter environment
  /// @param[in] cube_abs_path Absolute path to the cube primitive
  /// @param[in] cube GeomCube primitive
  /// @param[in] material_path Material path for the cube
  /// @param[in] subset_material_path_map Material subset map
  /// @param[in] rmaterial_map Material ID map
  /// @param[in] material_subsets GeomSubset array
  /// @param[in] blendshapes BlendShape array
  /// @param[out] dst RenderMesh output
  ///
  /// @return true when success.
  ///
  bool ConvertCube(
      const RenderSceneConverterEnv &env, const tinyusdz::Path &cube_abs_path,
      const tinyusdz::GeomCube &cube, const MaterialPath &material_path,
      const std::map<std::string, MaterialPath> &subset_material_path_map,
      const StringAndIdMap &rmaterial_map,
      const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
      const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
          &blendshapes,
      RenderMesh *dst);

  ///
  /// Convert GeomSphere to RenderMesh by generating tessellated geometry
  ///
  /// @param[in] env Converter environment
  /// @param[in] sphere_abs_path Absolute path to the sphere primitive
  /// @param[in] sphere GeomSphere primitive
  /// @param[in] material_path Material path for the sphere
  /// @param[in] subset_material_path_map Material subset map
  /// @param[in] rmaterial_map Material ID map
  /// @param[in] material_subsets GeomSubset array
  /// @param[in] blendshapes BlendShape array
  /// @param[out] dst RenderMesh output
  ///
  /// @return true when success.
  ///
  bool ConvertSphere(
      const RenderSceneConverterEnv &env, const tinyusdz::Path &sphere_abs_path,
      const tinyusdz::GeomSphere &sphere, const MaterialPath &material_path,
      const std::map<std::string, MaterialPath> &subset_material_path_map,
      const StringAndIdMap &rmaterial_map,
      const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
      const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
          &blendshapes,
      RenderMesh *dst);

  bool ConvertMesh(
      const RenderSceneConverterEnv &env, const tinyusdz::Path &mesh_abs_path,
      const tinyusdz::GeomMesh &mesh, const MaterialPath &material_path,
      const std::map<std::string, MaterialPath> &subset_material_path_map,
      //const std::map<std::string, int64_t> &rmaterial_map,
      const StringAndIdMap &rmaterial_map,
      const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
      const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
          &blendshapes,
      RenderMesh *dst);

  ///
  /// Convert USD Material/Shader to renderer-friendly Material
  ///
  /// @return true when success.
  ///
  bool ConvertMaterial(const RenderSceneConverterEnv &env,
                       const tinyusdz::Path &abs_mat_path,
                       const tinyusdz::Material &material,
                       RenderMaterial *rmat_out);

  ///
  /// Convert UsdPreviewSurface Shader to renderer-friendly PreviewSurfaceShader
  ///
  /// @param[in] shader_abs_path USD Path to Shader Prim with UsdPreviewSurface
  /// info:id.
  /// @param[in] shader UsdPreviewSurface
  /// @param[in] pss_put PreviewSurfaceShader
  ///
  /// @return true when success.
  ///
  bool ConvertPreviewSurfaceShader(const RenderSceneConverterEnv &env,
                                   const tinyusdz::Path &shader_abs_path,
                                   const tinyusdz::UsdPreviewSurface &shader,
                                   PreviewSurfaceShader *pss_out);
  
  ///
  /// Convert MaterialX OpenPBR Surface Shader to renderer-friendly OpenPBRSurfaceShader
  ///
  /// @param[in] env Conversion environment
  /// @param[in] shader_abs_path USD Path to Shader Prim with OpenPBRSurface info:id
  /// @param[in] shader OpenPBRSurface
  /// @param[out] openpbr_out OpenPBRSurfaceShader
  ///
  /// @return true when success.
  ///
  bool ConvertOpenPBRSurfaceShader(const RenderSceneConverterEnv &env,
                                    const tinyusdz::Path &shader_abs_path,
                                    const tinyusdz::OpenPBRSurface &shader,
                                    OpenPBRSurfaceShader *openpbr_out);

  ///
  /// Convert UsdUvTexture to renderer-friendly UVTexture
  ///
  /// @param[in] tex_abs_path USD Path to Shader Prim with UsdUVTexture info:id.
  /// @param[in] assetInfo assetInfo Prim metadata of given Shader Prim
  /// @param[in] texture UsdUVTexture
  /// @param[in] tex_out UVTexture
  ///
  /// TODO: Retrieve assetInfo from `tex_abs_path`?
  ///
  /// @return true when success.
  ///
  bool ConvertUVTexture(const RenderSceneConverterEnv &env,
                        const Path &tex_abs_path, const AssetInfo &assetInfo,
                        const UsdUVTexture &texture, UVTexture *tex_out);

  ///
  /// Convert SkelAnimation to Tydra Animation.
  ///
  /// @param[in] abs_path USD Path to SkelAnimation Prim
  /// @param[in] skelAnim SkelAnimation
  /// @param[in] anim_out AnimationClip
  ///
  bool ConvertSkelAnimation(const RenderSceneConverterEnv &env,
                        const Path &abs_path, const SkelAnimation &skelAnim,
                        int32_t skeleton_id,
                        AnimationClip *anim_out);

  ///
  /// Extract animation data from xformOps time samples and convert to AnimationClip
  ///
  /// @param[in] env Converter environment
  /// @param[in] abs_path Absolute path to the prim
  /// @param[in] prim_name Prim name
  /// @param[in] xformable Xformable object containing xformOps
  /// @param[in] target_node_index Index of the target node in RenderScene
  /// @param[out] anim_out Output AnimationClip
  /// @return true if animation was extracted, false otherwise
  ///
  bool ExtractXformOpAnimation(const RenderSceneConverterEnv &env,
                        const Path &abs_path,
                        const std::string &prim_name,
                        const Xformable &xformable,
                        int32_t target_node_index,
                        AnimationClip *anim_out);

  ///
  /// @param[in] env
  /// @param[in] root XformNode
  ///
  bool BuildNodeHierarchy(const RenderSceneConverterEnv &env, const XformNode &node);

  ///
  /// Convert UsdLux lights to renderer-friendly RenderLight
  ///

  bool ConvertSphereLight(const RenderSceneConverterEnv &env,
                          const Path &light_abs_path,
                          const SphereLight &light,
                          RenderLight *rlight_out);

  bool ConvertDistantLight(const RenderSceneConverterEnv &env,
                           const Path &light_abs_path,
                           const DistantLight &light,
                           RenderLight *rlight_out);

  bool ConvertDomeLight(const RenderSceneConverterEnv &env,
                        const Path &light_abs_path,
                        const DomeLight &light,
                        RenderLight *rlight_out);

  bool ConvertRectLight(const RenderSceneConverterEnv &env,
                        const Path &light_abs_path,
                        const RectLight &light,
                        RenderLight *rlight_out);

  bool ConvertDiskLight(const RenderSceneConverterEnv &env,
                        const Path &light_abs_path,
                        const DiskLight &light,
                        RenderLight *rlight_out);

  bool ConvertCylinderLight(const RenderSceneConverterEnv &env,
                            const Path &light_abs_path,
                            const CylinderLight &light,
                            RenderLight *rlight_out);

  bool ConvertGeometryLight(const RenderSceneConverterEnv &env,
                            const Path &light_abs_path,
                            const GeometryLight &light,
                            RenderLight *rlight_out);

 private:
  ///
  /// Convert variability of vertex data to 'vertex' or 'facevarying'.
  ///
  /// @param[inout] vattr Input/Output VertexAttribute
  /// @param[in] to_vertex_varing Convert to `vertex` varying when true.
  /// `facevarying` when false.
  /// @param[in] faceVertexCounts faceVertexCounts
  /// @param[in] faceVertexIndices faceVertexIndices
  ///
  /// @return true upon success.
  ///
  bool ConvertVertexVariabilityImpl(
      VertexAttribute &vattr, const bool to_vertex_varying,
      const std::vector<uint32_t> &faceVertexCounts,
      const std::vector<uint32_t> &faceVertexIndices);

  template <typename T, typename Dty>
  bool ConvertPreviewSurfaceShaderParam(
      const RenderSceneConverterEnv &env, const Path &shader_abs_path,
      const TypedAttributeWithFallback<Animatable<T>> &param,
      const std::string &param_name, ShaderParam<Dty> &dst_param,
      bool is_materialx = false);

  ///
  /// Build (single) vertex indices for RenderMesh.
  /// existing `RenderMesh::faceVertexIndices` will be replaced with built indices.
  /// All vertex attributes are converted to 'vertex' variability.
  ///
  /// Limitation: Currently we only supports texcoords up to two(primary(0) and secondary(1)).
  ///
  /// @param[inout] mesh
  ///
  bool BuildVertexIndicesImpl(RenderMesh &mesh);

  ///
  /// Build (single) vertex indices for RenderMesh.
  /// Skip similarity search for faster processing.
  /// existing `RenderMesh::faceVertexIndices` will be replaced with built indices.
  /// All vertex attributes are converted to 'vertex' variability.
  ///
  /// Limitation: Currently we only supports texcoords up to two(primary(0) and secondary(1)).
  ///
  /// @param[inout] mesh
  ///
  bool BuildVertexIndicesFastImpl(RenderMesh &mesh);

  //
  // Convert skeleton from explicit path (for ancestor-discovered skeletons)
  bool ConvertSkeletonImplWithPath(const RenderSceneConverterEnv &env, const Path &skelPath,
                       SkelHierarchy *out_skel);

  // Convert skeleton from Skeleton pointer directly (more efficient for pre-discovered skeletons)
  bool ConvertSkeletonFromPtr(const RenderSceneConverterEnv &env,
                       const Path &skelPath,
                       const Skeleton &skel,
                       const std::string &primName,
                       SkelHierarchy *out_skel);

  // Convert all SkelAnimation prims after skeleton conversion is complete.
  // Supports multiple animations per skeleton by processing all discovered SkelAnimation prims
  // and finding which Skeleton(s) reference each via their skel:animationSource relationship.
  bool ConvertAllSkelAnimations(const RenderSceneConverterEnv &env);

  bool BuildNodeHierarchyImpl(
    const RenderSceneConverterEnv &env,
    const std::string &parentPrimPath,
    const XformNode &node,
    Node &out_rnode);

  ///
  /// Merge meshes with the same material for performant rendering.
  ///
  /// This function merges meshes that share the same material and have
  /// compatible properties into a single mesh. It reduces draw calls
  /// for GPU-based renderers.
  ///
  /// @param[in] env Converter environment containing configuration
  /// @param[inout] nodes Node hierarchy (mesh node IDs will be updated)
  /// @param[inout] meshes Mesh array (merged meshes will be added, originals marked)
  ///
  /// @return true upon success
  ///
  bool MergeMeshesImpl(const RenderSceneConverterEnv &env);

  ///
  /// Helper to check if a mesh can be merged with others.
  /// Returns true if the mesh has no skeletal animation, no blend shapes,
  /// and no per-face materials.
  ///
  bool IsMeshMergeable(const RenderMesh &mesh) const;

  ///
  /// Merge two RenderMesh instances into a single mesh.
  /// The source mesh data is appended to the destination mesh.
  ///
  /// @param[in] src Source mesh to merge from
  /// @param[in] src_transform Transform to apply to source vertices (identity if no transform baking)
  /// @param[inout] dst Destination mesh to merge into
  ///
  /// @return true upon success
  ///
  bool MergeMeshData(const RenderMesh &src, const value::matrix4d &src_transform,
                     RenderMesh &dst);

  void PushInfo(const std::string &msg) { _info += msg + "\n"; }
  void PushWarn(const std::string &msg) { _warn += msg + "\n"; }
  void PushError(const std::string &msg) { _err += msg + "\n"; }

  ///
  /// Call progress callback if set.
  /// @param[in] progress Progress value between 0.0 and 1.0
  /// @return true to continue, false to cancel
  ///
  bool CallProgressCallback(float progress);

  ///
  /// Call detailed progress callback if set.
  /// @param[in] info Detailed progress information
  /// @return true to continue, false to cancel
  ///
  bool CallDetailedProgressCallback(const DetailedProgressInfo &info);

  std::string _info;
  std::string _err;
  std::string _warn;

  // Progress callback
  ProgressCallback _progress_callback{nullptr};
  void *_progress_userptr{nullptr};

  // Detailed progress callback
  DetailedProgressCallback _detailed_progress_callback{nullptr};
  void *_detailed_progress_userptr{nullptr};

  // Progress state for detailed tracking
  mutable DetailedProgressInfo _progress_info;

  // Reusable buffers for mesh conversion to avoid repeated allocation
  mutable std::vector<value::float3> _tmp_points_buffer;

  // Lookup caches for O(1) skeleton/animation dedup (populated during ConvertToRenderScene)
  std::unordered_map<std::string, int32_t> _skelPathToIndex;
  std::unordered_map<std::string, int32_t> _animPathToIndex;

  // Cached BuildSkelNameToIndexMap results per skeleton ID
  std::unordered_map<int32_t, std::map<std::string, int>> _skelNameToIndexCache;

  // Precomputed SkelRoot -> Skeleton mapping for fast ancestor discovery.
  std::unordered_map<std::string, std::pair<Path, const Skeleton *>> _skelRootToSkeleton;

  // Cached ListUVNames results per material ID
  std::unordered_map<int64_t, StringAndIdMap> _uvNameCache;
};

///
/// Dump RenderScene to string (for debugging)
///
/// Supported formats:
/// - "yaml" (default) - Human-readable YAML format with metadata header
/// - "json" - Machine-readable JSON format with metadata header
/// - "kdl"  - Original KDL format (https://kdl.dev/)
///
/// Both YAML and JSON formats include:
/// - Metadata section (format_version, generator, source_file, scene settings)
/// - Summary section (counts of nodes, meshes, materials, etc.)
/// - Full scene data (nodes, meshes, skeletons, animations, cameras, materials, textures, images, buffers)
///
std::string DumpRenderScene(const RenderScene &scene,
                            const std::string &format = "yaml");

}  // namespace tydra
}  // namespace tinyusdz
