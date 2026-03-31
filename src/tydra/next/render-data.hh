// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Data Structures
//
// Design goals:
// - Minimal intermediate copies
// - Chunked arrays for WASM heap efficiency
// - GPU-friendly data layout
// - Runtime type dispatch (no templates in API)

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "chunked-array.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

//
// Forward declarations
//
class RenderScene;
class RenderMesh;
class RenderMaterial;
class RenderTexture;

//
// Enums
//

enum class ColorSpace : uint8_t {
  sRGB = 0,
  Linear,
  Raw,
  ACEScg,
  Rec709,
  Rec2020,
  DisplayP3,
  Unknown
};

enum class WrapMode : uint8_t {
  Repeat = 0,
  Clamp,
  Mirror,
  Black
};

enum class ComponentType : uint8_t {
  UInt8 = 0,
  Int8,
  UInt16,
  Int16,
  UInt32,
  Int32,
  Float16,
  Float32,
  Float64
};

enum class VertexFormat : uint8_t {
  Float = 0,
  Vec2,
  Vec3,
  Vec4,
  Int,
  IVec2,
  IVec3,
  IVec4,
  UInt,
  UVec2,
  UVec3,
  UVec4
};

enum class Interpolation : uint8_t {
  Constant = 0,   // One value for entire prim
  Uniform,        // One value per face
  Vertex,         // One value per vertex (shared)
  FaceVarying,    // One value per face-vertex
  Varying         // Bilinear interpolation
};

enum class NodeType : uint8_t {
  Xform = 0,
  Mesh,
  Camera,
  PointLight,
  DirectionalLight,
  SpotLight,
  RectLight,
  DiskLight,
  DomeLight,
  SphereLight,
  Skeleton
};

enum class LightType : uint8_t {
  Point = 0,
  Directional,
  Spot,
  Rect,
  Disk,
  Dome,
  Sphere,
  Cylinder,
  Geometry
};

enum class CameraType : uint8_t {
  Perspective = 0,
  Orthographic
};

//
// Basic math types (GPU-aligned)
//

struct alignas(16) Float3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float _pad = 0.0f;

  Float3() = default;
  Float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Float2 {
  float x = 0.0f, y = 0.0f;

  Float2() = default;
  Float2(float x_, float y_) : x(x_), y(y_) {}
};

struct alignas(16) Float4 {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

  Float4() = default;
  Float4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct alignas(64) Matrix4 {
  float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

  static Matrix4 Identity() { return Matrix4(); }
  float& operator()(int row, int col) { return m[row * 4 + col]; }
  float operator()(int row, int col) const { return m[row * 4 + col]; }
};

//
// Vertex Attribute
//
// Stores raw vertex data with format and interpolation info
//
struct VertexAttribute {
  std::string name;
  VertexFormat format = VertexFormat::Vec3;
  Interpolation interpolation = Interpolation::Vertex;

  // Raw data storage (format-dependent)
  FloatChunked float_data;     // For Float, Vec2, Vec3, Vec4
  Int32Chunked int_data;       // For Int, IVec2, IVec3, IVec4
  UInt32Chunked uint_data;     // For UInt, UVec2, UVec3, UVec4

  // Optional index buffer (for indexed attributes)
  UInt32Chunked indices;

  bool has_indices() const { return !indices.empty(); }

  size_t element_count() const {
    switch (format) {
      case VertexFormat::Float:
      case VertexFormat::Int:
      case VertexFormat::UInt:
        return float_data.size();
      case VertexFormat::Vec2:
      case VertexFormat::IVec2:
      case VertexFormat::UVec2:
        return float_data.size() / 2;
      case VertexFormat::Vec3:
      case VertexFormat::IVec3:
      case VertexFormat::UVec3:
        return float_data.size() / 3;
      case VertexFormat::Vec4:
      case VertexFormat::IVec4:
      case VertexFormat::UVec4:
        return float_data.size() / 4;
    }
    return 0;
  }

  size_t memory_usage() const {
    return float_data.memory_usage() + int_data.memory_usage() +
           uint_data.memory_usage() + indices.memory_usage();
  }
};

//
// RenderMesh - GPU-ready mesh data
//
struct RenderMesh {
  std::string name;
  std::string prim_path;

  // Topology
  UInt32Chunked face_vertex_counts;   // Number of verts per face
  UInt32Chunked face_vertex_indices;  // Vertex indices

  // Primary vertex data (always present for valid mesh)
  FloatChunked points;  // xyz interleaved, size = num_points * 3

  // Optional computed/loaded attributes
  FloatChunked normals;           // xyz, may be per-vertex or per-face-vertex
  FloatChunked tangents;          // xyzw (w = handedness sign)
  FloatChunked texcoords_0;       // Primary UV (st), xy interleaved
  FloatChunked texcoords_1;       // Secondary UV
  FloatChunked colors;            // Vertex colors (rgb or rgba)

  // Interpolation modes for attributes
  Interpolation normals_interp = Interpolation::Vertex;
  Interpolation texcoords_0_interp = Interpolation::Vertex;
  Interpolation texcoords_1_interp = Interpolation::Vertex;
  Interpolation colors_interp = Interpolation::Vertex;

  // Additional primvars (custom attributes)
  std::vector<VertexAttribute> primvars;

  // Material assignment
  int32_t material_id = -1;  // -1 = no material

  // Per-face material (GeomSubset)
  // Maps face range [start, end) to material_id
  struct MaterialSubset {
    uint32_t face_start;
    uint32_t face_count;
    int32_t material_id;
  };
  std::vector<MaterialSubset> material_subsets;

  // Skinning data
  struct SkinBinding {
    UInt16Chunked joint_indices;  // 4 joints per vertex
    FloatChunked joint_weights;   // 4 weights per vertex
    int32_t skeleton_id = -1;
    Matrix4 geom_bind_transform;
  };
  std::unique_ptr<SkinBinding> skin;

  // Blend shapes
  struct BlendShape {
    std::string name;
    FloatChunked point_offsets;   // xyz deltas
    FloatChunked normal_offsets;  // xyz deltas (optional)
    float weight = 0.0f;
  };
  std::vector<BlendShape> blend_shapes;

  // Triangulated data (computed on demand)
  UInt32Chunked triangulated_indices;
  bool is_triangulated = false;

  // Bounding box
  Float3 bbox_min;
  Float3 bbox_max;
  bool has_bbox = false;

  // Helpers
  size_t point_count() const { return points.size() / 3; }
  size_t face_count() const { return face_vertex_counts.size(); }
  bool has_normals() const { return !normals.empty(); }
  bool has_tangents() const { return !tangents.empty(); }
  bool has_texcoords() const { return !texcoords_0.empty(); }
  bool has_colors() const { return !colors.empty(); }
  bool has_skin() const { return skin != nullptr; }
  bool has_blend_shapes() const { return !blend_shapes.empty(); }

  size_t memory_usage() const;
};

//
// Shader Parameter - value or texture reference
//
struct ShaderParam {
  // If texture_id >= 0, use texture; otherwise use value
  int32_t texture_id = -1;

  // Scalar/vector value (when texture_id < 0)
  Float4 value = {0, 0, 0, 1};

  bool is_texture() const { return texture_id >= 0; }
  bool is_value() const { return texture_id < 0; }

  float as_float() const { return value.x; }
  Float3 as_float3() const { return Float3(value.x, value.y, value.z); }
  Float4 as_float4() const { return value; }
};

//
// PreviewSurface shader parameters
//
struct PreviewSurfaceShader {
  ShaderParam diffuse_color = {{-1}, {0.18f, 0.18f, 0.18f, 1.0f}};
  ShaderParam emissive_color = {{-1}, {0, 0, 0, 1}};
  ShaderParam specular_color = {{-1}, {1, 1, 1, 1}};

  ShaderParam metallic = {{-1}, {0, 0, 0, 0}};
  ShaderParam roughness = {{-1}, {0.5f, 0, 0, 0}};
  ShaderParam clearcoat = {{-1}, {0, 0, 0, 0}};
  ShaderParam clearcoat_roughness = {{-1}, {0.01f, 0, 0, 0}};

  ShaderParam opacity = {{-1}, {1, 0, 0, 0}};
  ShaderParam opacity_threshold = {{-1}, {0, 0, 0, 0}};
  ShaderParam ior = {{-1}, {1.5f, 0, 0, 0}};

  ShaderParam normal = {{-1}, {0, 0, 1, 0}};
  ShaderParam displacement = {{-1}, {0, 0, 0, 0}};
  ShaderParam occlusion = {{-1}, {1, 0, 0, 0}};

  bool use_specular_workflow = false;
};

//
// OpenPBR Surface shader (MaterialX)
//
struct OpenPBRSurfaceShader {
  // Base
  ShaderParam base_weight = {{-1}, {1, 0, 0, 0}};
  ShaderParam base_color = {{-1}, {0.8f, 0.8f, 0.8f, 1}};
  ShaderParam base_roughness = {{-1}, {0, 0, 0, 0}};
  ShaderParam base_metalness = {{-1}, {0, 0, 0, 0}};

  // Specular
  ShaderParam specular_weight = {{-1}, {1, 0, 0, 0}};
  ShaderParam specular_color = {{-1}, {1, 1, 1, 1}};
  ShaderParam specular_roughness = {{-1}, {0.3f, 0, 0, 0}};
  ShaderParam specular_ior = {{-1}, {1.5f, 0, 0, 0}};
  ShaderParam specular_anisotropy = {{-1}, {0, 0, 0, 0}};
  ShaderParam specular_rotation = {{-1}, {0, 0, 0, 0}};

  // Transmission
  ShaderParam transmission_weight = {{-1}, {0, 0, 0, 0}};
  ShaderParam transmission_color = {{-1}, {1, 1, 1, 1}};
  ShaderParam transmission_depth = {{-1}, {0, 0, 0, 0}};

  // Subsurface
  ShaderParam subsurface_weight = {{-1}, {0, 0, 0, 0}};
  ShaderParam subsurface_color = {{-1}, {0.8f, 0.8f, 0.8f, 1}};
  ShaderParam subsurface_radius = {{-1}, {1, 1, 1, 0}};

  // Coat
  ShaderParam coat_weight = {{-1}, {0, 0, 0, 0}};
  ShaderParam coat_color = {{-1}, {1, 1, 1, 1}};
  ShaderParam coat_roughness = {{-1}, {0, 0, 0, 0}};
  ShaderParam coat_ior = {{-1}, {1.5f, 0, 0, 0}};

  // Sheen
  ShaderParam sheen_weight = {{-1}, {0, 0, 0, 0}};
  ShaderParam sheen_color = {{-1}, {1, 1, 1, 1}};
  ShaderParam sheen_roughness = {{-1}, {0.3f, 0, 0, 0}};

  // Emission
  ShaderParam emission_luminance = {{-1}, {0, 0, 0, 0}};
  ShaderParam emission_color = {{-1}, {1, 1, 1, 1}};

  // Geometry
  ShaderParam opacity = {{-1}, {1, 0, 0, 0}};
  ShaderParam normal = {{-1}, {0, 0, 1, 0}};
  ShaderParam tangent = {{-1}, {1, 0, 0, 0}};

  // MaterialX node graph as JSON (optional)
  std::string nodegraph_json;
};

//
// RenderMaterial
//
struct RenderMaterial {
  std::string name;
  std::string prim_path;

  // Shader type
  enum class ShaderType : uint8_t {
    None = 0,
    PreviewSurface,
    OpenPBR
  };
  ShaderType shader_type = ShaderType::None;

  // Shader data (one of these based on shader_type)
  std::unique_ptr<PreviewSurfaceShader> preview_surface;
  std::unique_ptr<OpenPBRSurfaceShader> openpbr;

  // Double-sided
  bool double_sided = false;

  // Alpha mode
  enum class AlphaMode : uint8_t {
    Opaque = 0,
    Mask,
    Blend
  };
  AlphaMode alpha_mode = AlphaMode::Opaque;
  float alpha_cutoff = 0.5f;
};

//
// RenderTexture
//
struct RenderTexture {
  std::string name;
  std::string prim_path;
  std::string asset_path;  // Original USD asset path

  // UV transform
  Float2 offset = {0, 0};
  Float2 scale = {1, 1};
  float rotation = 0.0f;  // Radians

  // Sampling
  WrapMode wrap_s = WrapMode::Repeat;
  WrapMode wrap_t = WrapMode::Repeat;

  // Bias/scale for texture values
  Float4 bias = {0, 0, 0, 0};
  Float4 scale_value = {1, 1, 1, 1};

  // Image reference
  int32_t image_id = -1;

  // Which channel to use (for single-channel textures)
  enum class Channel : uint8_t { R = 0, G, B, A, RGB, RGBA };
  Channel output_channel = Channel::RGBA;
};

//
// TextureImage - actual image data
//
struct TextureImage {
  std::string name;
  std::string resolved_path;  // Filesystem path

  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t channels = 4;
  uint8_t mip_levels = 1;

  ComponentType component_type = ComponentType::UInt8;
  ColorSpace color_space = ColorSpace::sRGB;

  // Raw image data (may be empty if not loaded)
  UInt8Chunked data;

  bool is_loaded() const { return !data.empty(); }
  size_t memory_usage() const { return data.memory_usage(); }
};

//
// RenderLight
//
struct RenderLight {
  std::string name;
  std::string prim_path;
  LightType type = LightType::Point;

  // Common properties
  Float3 color = {1, 1, 1};
  float intensity = 1.0f;
  float exposure = 0.0f;
  bool normalize = false;

  // Transform
  Matrix4 transform;

  // Type-specific properties
  union {
    struct { float radius; } sphere;
    struct { float width, height; } rect;
    struct { float radius; } disk;
    struct { float angle; } spot;  // Cone angle in radians
    struct { int32_t texture_id; } dome;
  } params = {};

  // Shadow
  bool enable_shadow = true;
  Float3 shadow_color = {0, 0, 0};
};

//
// RenderCamera
//
struct RenderCamera {
  std::string name;
  std::string prim_path;
  CameraType type = CameraType::Perspective;

  // Transform
  Matrix4 transform;

  // Perspective params
  float focal_length = 50.0f;        // mm
  float horizontal_aperture = 36.0f; // mm
  float vertical_aperture = 24.0f;   // mm

  // Orthographic params
  float ortho_width = 10.0f;

  // Clipping
  float near_clip = 0.1f;
  float far_clip = 10000.0f;

  // Computed FOV
  float fov_y() const;
  float fov_x() const;
  float aspect_ratio() const;
};

//
// Scene Node (transform hierarchy)
//
struct SceneNode {
  std::string name;
  std::string prim_path;
  NodeType type = NodeType::Xform;

  // Transform
  Matrix4 local_transform;
  Matrix4 world_transform;

  // Reference to data (based on type)
  int32_t data_id = -1;  // Index into appropriate array

  // Hierarchy
  int32_t parent_id = -1;
  std::vector<int32_t> children;

  // Visibility
  bool visible = true;
};

//
// Animation Keyframe
//
struct Keyframe {
  double time;
  Float4 value;  // Supports up to vec4
};

//
// Animation Channel
//
struct AnimationChannel {
  enum class TargetPath : uint8_t {
    Translation = 0,
    Rotation,
    Scale,
    Weights  // Blend shape weights
  };

  TargetPath target_path = TargetPath::Translation;
  int32_t target_node = -1;

  // Keyframes (sorted by time)
  std::vector<Keyframe> keyframes;

  // Interpolation
  enum class Interpolation : uint8_t {
    Step = 0,
    Linear,
    CubicSpline
  };
  Interpolation interpolation = Interpolation::Linear;
};

//
// Animation Clip
//
struct AnimationClip {
  std::string name;
  std::string prim_path;

  double start_time = 0.0;
  double end_time = 0.0;

  std::vector<AnimationChannel> channels;
};

//
// Skeleton Joint
//
struct SkeletonJoint {
  std::string name;
  std::string path;
  int32_t parent_id = -1;

  Matrix4 bind_transform;
  Matrix4 rest_transform;

  std::vector<int32_t> children;
};

//
// Skeleton
//
struct Skeleton {
  std::string name;
  std::string prim_path;

  std::vector<SkeletonJoint> joints;
  int32_t root_joint = 0;

  // Animation reference
  int32_t animation_id = -1;
};

//
// RenderScene - top-level container
//
class RenderScene {
 public:
  RenderScene() = default;
  ~RenderScene() = default;

  // Move only
  RenderScene(RenderScene&&) = default;
  RenderScene& operator=(RenderScene&&) = default;
  RenderScene(const RenderScene&) = delete;
  RenderScene& operator=(const RenderScene&) = delete;

  // Scene metadata
  std::string name;
  std::string default_prim;
  float meters_per_unit = 1.0f;
  enum class UpAxis : uint8_t { Y = 0, Z } up_axis = UpAxis::Y;

  // Time range
  double start_time = 0.0;
  double end_time = 0.0;
  double frames_per_second = 24.0;

  // Scene data
  std::vector<SceneNode> nodes;
  std::vector<RenderMesh> meshes;
  std::vector<RenderMaterial> materials;
  std::vector<RenderTexture> textures;
  std::vector<TextureImage> images;
  std::vector<RenderLight> lights;
  std::vector<RenderCamera> cameras;
  std::vector<AnimationClip> animations;
  std::vector<Skeleton> skeletons;

  // Root nodes
  std::vector<int32_t> root_nodes;

  // Lookup by path
  std::unordered_map<std::string, int32_t> node_by_path;
  std::unordered_map<std::string, int32_t> mesh_by_path;
  std::unordered_map<std::string, int32_t> material_by_path;

  // Memory usage
  size_t memory_usage() const;

  // Stats
  struct Stats {
    size_t node_count;
    size_t mesh_count;
    size_t material_count;
    size_t texture_count;
    size_t image_count;
    size_t light_count;
    size_t camera_count;
    size_t animation_count;
    size_t skeleton_count;
    size_t total_vertices;
    size_t total_triangles;
    size_t memory_bytes;
  };
  Stats get_stats() const;
};

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
