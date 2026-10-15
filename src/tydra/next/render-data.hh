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
#include <utility>

#include "chunked-array.hh"

namespace lightusd {
namespace tydra {
namespace next {

//
// Forward declarations
//
class RenderScene;
struct RenderMesh;
struct RenderMaterial;
struct RenderTexture;

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
  UVec4,
  Matrix33,
  Matrix44
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
  Points,
  PointInstancer,
  Camera,
  PointLight,
  DirectionalLight,
  SpotLight,
  RectLight,
  DiskLight,
  DomeLight,
  SphereLight,
  Skeleton,
  Curves
};

//
// Curve enums (UsdGeomBasisCurves / UsdGeomNurbsCurves)
//

enum class CurveType : uint8_t {
  Linear = 0,  // Polyline segments between control points
  Cubic        // Cubic spans (see CurveBasis)
};

enum class CurveBasis : uint8_t {
  Bezier = 0,
  BSpline,
  CatmullRom
};

enum class CurveWrap : uint8_t {
  Nonperiodic = 0,
  Periodic,   // Curve closes back onto its first point
  Pinned      // bspline/catmullRom interpolate their end control points
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

#include "render-data-math.hh"


//
// Vertex Attribute
#include "render-data-vertex.hh"
#include "render-data-mesh.hh"

#include "render-data-points.hh"

//
// Shader Parameter - value or texture reference
#include "render-data-shaders.hh"

struct MaterialDiagnostic {
  MaterialDiagnosticKind kind = MaterialDiagnosticKind::UnsupportedShader;
  std::string material_path;
  std::string node_path;
  std::string shader_id;
  std::string message;
};

// Authored shader inputs retained when the surface terminal cannot be fully
// evaluated. These values are intentionally neutral to current real-time
// shading, but remain available to future evaluators and diagnostics.
struct RetainedMaterialParam {
  std::string shader;
  std::string name;
  ShaderParam value;
};

struct RenderMaterial {
  RenderMaterial();
  ~RenderMaterial();
  RenderMaterial(const RenderMaterial&);
  RenderMaterial& operator=(const RenderMaterial&);
  RenderMaterial(RenderMaterial&&) noexcept;
  RenderMaterial& operator=(RenderMaterial&&) noexcept;

  std::string name;
  std::string prim_path;

  struct MaterialXConfig {
    bool authored = false;
    std::string version;
    std::string name_space;
    std::string colorspace;
    std::string source_uri;
  };

  // Shader type
  enum class ShaderType : uint8_t {
    None = 0,
    PreviewSurface,
    OpenPBR
  };
  ShaderType shader_type = ShaderType::None;

  // The material had no fully convertible surface shader and carries a
  // degraded PreviewSurface instead of being dropped (see ConvertMaterial).
  // Recognizable authored constants and texture inputs are retained when
  // possible. Conversion still SUCCEEDS, so consumers that report load
  // degradation must look here rather than relying on the return value.
  bool default_fallback = false;
  std::vector<MaterialDiagnostic> diagnostics;
  std::vector<RetainedMaterialParam> retained_params;

  // Shader data (one of these based on shader_type)
  std::shared_ptr<PreviewSurfaceShader> preview_surface;
  std::shared_ptr<OpenPBRSurfaceShader> openpbr;
  // Authored utility graph feeding a PreviewSurface terminal. Kept as a
  // separate compatibility payload because the GPU-facing parameters above
  // intentionally contain only the resolved shader values.
  std::string preview_surface_nodegraph_json;

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

  // Material displacement/volume terminals (outputs:displacement /
  // outputs:volume connections). Terminal paths remain available for geometry
  // consumers; MaterialX volume topology is retained separately below.
  bool has_displacement = false;
  std::string displacement_shader_path;
  bool has_volume = false;
  std::string volume_shader_path;
  // Retained MaterialX graph for the volume terminal. Kept separate from the
  // surface graph because a material may author both terminals.
  std::string volume_nodegraph_json;

  MaterialXConfig mtlx_config;
};

#include "render-data-texture.hh"

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
  bool enable_color_temperature = false;
  float color_temperature = 6500.0f;
  float diffuse = 1.0f;
  float specular = 1.0f;
  float shaping_cone_angle = 90.0f;
  float shaping_focus = 0.0f;
  Float3 shaping_focus_tint = {0, 0, 0};  // color3f per UsdLux ShapingAPI
  float shaping_cone_softness = 0.0f;
  std::string shaping_ies_file;
  // Authored texture asset on RectLight. Dome textures use the resolved image
  // table and params.dome.texture_id instead.
  std::string texture_file;
  float shaping_ies_angle_scale = 0.0f;
  bool shaping_ies_normalize = false;
  std::vector<std::string> light_link_targets;
  std::vector<std::string> shadow_link_targets;
  std::vector<std::string> filter_targets;

  // Light/shadow linking resolved to RenderScene mesh indices (CollectionAPI
  // collection:lightLink / collection:shadowLink relationship form). When the
  // collection is unauthored the light links everything and *_links_all stays
  // true. Relationship and membershipExpression collection forms are both
  // resolved by the converter.
  bool light_links_all = true;
  std::vector<int32_t> light_link_mesh_indices;
  bool shadow_links_all = true;
  std::vector<int32_t> shadow_link_mesh_indices;

  // DomeLight inputs:texture:format token (matches UsdLux).
  enum class DomeTextureFormat : uint8_t {
    Automatic = 0,
    Latlong,
    MirroredBall,
    Angular,
  };

  // Transform
  Matrix4 transform;

  // Preserve the SphereLight radius when shaping makes its render type Spot.
  float spot_source_radius = 0.5f;
  float guide_radius = 1.0e5f;

  // Type-specific properties
  union {
    struct { float radius; } sphere;
    struct { float width, height; } rect;
    struct { float radius; } disk;
    struct { float angle; } spot;  // Cone angle in radians
    struct { int32_t texture_id; DomeTextureFormat texture_format; } dome;
    struct { float radius, length; } cylinder;
    struct { float angle; } distant;  // Angular size in degrees
  } params = {};

  // Shadow
  bool enable_shadow = true;
  Float3 shadow_color = {0, 0, 0};
  float shadow_distance = -1.0f;
  float shadow_falloff = -1.0f;
  float shadow_falloff_gamma = 1.0f;
};

//
// RenderCamera
//
struct RenderBackPlate {
  std::string instance_name;
  std::string image;
  std::string alpha_image;
  std::string depth_image;
  float depth_min_offset = 0.0f;
  float depth_normalizing_factor = 1.0f;
  float depth_camera_space_offset = 0.0f;
  Float2 scale_tweak = {1.0f, 1.0f};
  Float3 rotate_xyz_tweak = {0.0f, 0.0f, 0.0f};
  Float3 translate_tweak = {0.0f, 0.0f, 0.0f};
  Float3 luma_gain = {1.0f, 1.0f, 1.0f};
  Float3 luma_lift = {0.0f, 0.0f, 0.0f};
  Float3 luma_gamma = {1.0f, 1.0f, 1.0f};
  std::string plate_visibility = "solo";
};

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
  float horizontal_aperture_offset = 0.0f;
  float vertical_aperture_offset = 0.0f;

  // Orthographic params
  float ortho_width = 10.0f;

  // Clipping
  float near_clip = 0.1f;
  float far_clip = 10000.0f;

  // Depth of field / exposure
  float focus_distance = 5.0f;
  float fstop = 0.0f;           // 0 = no DoF
  float exposure = 0.0f;

  enum class StereoRole : uint8_t { Mono = 0, Left, Right };
  StereoRole stereo_role = StereoRole::Mono;
  std::vector<Float4> clipping_planes;
  std::vector<RenderBackPlate> back_plates;

  // Motion-blur shutter interval (UsdGeomCamera shutter:open/close), in
  // time-code offsets relative to the sample time.
  double shutter_open = 0.0;
  double shutter_close = 0.0;

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
  std::string prototype_path;
  NodeType type = NodeType::Xform;
  bool has_reset_xform = false;
  bool is_instance = false;

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
    Weights,  // Blend shape weights
    CustomProperty
  };

  TargetPath target_path = TargetPath::Translation;
  int32_t target_node = -1;
  int32_t target_skeleton = -1;
  std::string target_prim_path;
  std::string property_name;
  std::string target_skeleton_path;
  std::vector<std::string> joint_order;
  std::vector<std::string> blend_shape_order;
  // Maps each SkelAnimation joint_order element to a Skeleton::joints index.
  // -1 means the animation joint could not be resolved on the target skeleton.
  std::vector<int32_t> joint_remap;

  // Keyframes (sorted by time)
  std::vector<Keyframe> keyframes;

  // Optional full array payload for UsdSkelAnimation channels. For frame i,
  // slice array_values at:
  //   i * element_count * value_stride
  // with element_count values, each value_stride floats wide.
  // Existing scalar keyframes keep a first-element preview for compatibility.
  std::vector<float> array_values;
  uint32_t element_count = 1;
  uint32_t value_stride = 4;
  bool is_skeletal = false;

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

  // Source value-clip metadata retained after baking for diagnostics and web
  // feature-parity reporting.
  std::vector<std::string> clip_asset_paths;
  bool value_clip_baked = false;
};

//
// Skeleton Joint
//
struct SkeletonJoint {
  std::string name;
  std::string path;
  int32_t parent_id = -1;

  Matrix4d bind_transform;
  Matrix4d rest_transform;

  std::vector<int32_t> children;
};

//
// Skeleton
//
struct Skeleton {
  std::string name;
  std::string display_name;
  std::string prim_path;

  std::vector<SkeletonJoint> joints;
  int32_t root_joint = 0;

  // Animation reference
  int32_t animation_id = -1;
  std::string animation_source_path;
};

//
// USD Physics annotations extracted from next::Stage.
// These are descriptive data for render/tool consumers; tydra-next does not
// simulate physics.
//
struct PhysicsProperty {
  std::string name;
  std::string value;
};

struct PhysicsSceneAnnotation {
  std::string prim_path;
  Float3 gravity_direction{0.0f, -1.0f, 0.0f};
  float gravity_magnitude = 9.81f;
  std::vector<PhysicsProperty> extension_properties;
};

struct PhysicsRigidBodyAnnotation {
  std::string prim_path;
  bool rigid_body_enabled = true;
  bool kinematic_enabled = false;
  std::string simulation_owner;
  Float3 velocity{0.0f, 0.0f, 0.0f};
  Float3 angular_velocity{0.0f, 0.0f, 0.0f};
  bool starts_asleep = false;
  bool has_mass = false;
  float mass = 0.0f;
  float density = 0.0f;
  Float3 center_of_mass{0.0f, 0.0f, 0.0f};
  Float3 diagonal_inertia{0.0f, 0.0f, 0.0f};
  Float4 principal_axes{0.0f, 0.0f, 0.0f, 0.0f};
  std::vector<PhysicsProperty> extension_properties;
};

struct PhysicsColliderAnnotation {
  std::string prim_path;
  bool collision_enabled = true;
  std::string simulation_owner;
  bool has_mesh_collision = false;
  std::string approximation = "none";
  std::vector<PhysicsProperty> extension_properties;
};

struct PhysicsJointAnnotation {
  std::string prim_path;
  std::string type_name;
  std::string body0;
  std::string body1;
  bool has_body0 = false;
  bool has_body1 = false;
  Float3 local_pos0{0.0f, 0.0f, 0.0f};
  Float3 local_pos1{0.0f, 0.0f, 0.0f};
  Float4 local_rot0{1.0f, 0.0f, 0.0f, 0.0f};
  Float4 local_rot1{1.0f, 0.0f, 0.0f, 0.0f};
  Float3 axis{1.0f, 0.0f, 0.0f};
  float lower_limit = 0.0f;
  float upper_limit = 0.0f;
  float cone_angle0_limit = -1.0f;
  float cone_angle1_limit = -1.0f;
  float min_distance = -1.0f;
  float max_distance = -1.0f;
  bool collision_enabled = false;
  std::vector<PhysicsProperty> extension_properties;
};

struct PhysicsMaterialAnnotation {
  std::string prim_path;
  float static_friction = 0.0f;
  float dynamic_friction = 0.0f;
  float restitution = 0.0f;
  float density = 0.0f;
  std::vector<PhysicsProperty> extension_properties;
};

struct PhysicsFilteredPairsAnnotation {
  std::string prim_path;
  std::vector<std::string> filtered_pair_paths;
};

struct PhysicsAnnotations {
  std::vector<PhysicsSceneAnnotation> scenes;
  std::vector<PhysicsRigidBodyAnnotation> rigid_bodies;
  std::vector<PhysicsColliderAnnotation> colliders;
  std::vector<PhysicsJointAnnotation> joints;
  std::vector<PhysicsMaterialAnnotation> materials;
  std::vector<PhysicsFilteredPairsAnnotation> filtered_pairs;
  std::vector<std::string> articulation_roots;
};

struct UnsupportedRenderable {
  std::string prim_path;
  std::string type_name;
  std::string reason;
};

enum class ConversionDisposition : uint8_t {
  PreservedUnsupported,
  Approximated,
  Failed
};

struct ConversionDiagnostic {
  ConversionDisposition disposition = ConversionDisposition::Failed;
  std::string prim_path;
  std::string message;
};

//
// RenderScene - top-level container
//
class RenderScene {
 public:
  RenderScene();
  ~RenderScene();

  // Copies share chunk-backed geometry copy-on-write. Catalog vectors are
  // copied, while optional skin/shader records remain shared and immutable;
  // transactional updates replace whole affected resources.
  RenderScene(RenderScene&&) noexcept;
  RenderScene& operator=(RenderScene&&) noexcept;
  RenderScene(const RenderScene&);
  RenderScene& operator=(const RenderScene&);

  // Scene metadata
  std::string name;
  std::string default_prim;
  float meters_per_unit = 1.0f;
  enum class UpAxis : uint8_t { Y = 0, Z } up_axis = UpAxis::Y;

  // OpenUSD color-management state. Shader color constants in this scene are
  // expressed in working_color_space. The row-major matrix maps linear values
  // from that space into Three.js/native display-linear Rec.709.
  std::string render_settings_path;
  std::string working_color_space = "lin_rec709_scene";
  float working_to_display_linear[9] = {
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};

  // Time range
  double start_time = 0.0;
  double end_time = 0.0;
  double frames_per_second = 24.0;

  // Scene data
  std::vector<SceneNode> nodes;
  std::vector<RenderMesh> meshes;
  std::vector<RenderPoints> points;
  std::vector<RenderCurves> curves;
  std::vector<RenderPointInstancer> point_instancers;
  std::vector<RenderPointInstanceDraw> point_instance_draws;
  std::vector<RenderMaterial> materials;
  std::vector<RenderTexture> textures;
  std::vector<RenderUDIMTexture> udim_textures;
  std::vector<TextureImage> images;
  std::vector<RenderLight> lights;
  std::vector<RenderCamera> cameras;
  std::vector<AnimationClip> animations;
  std::vector<Skeleton> skeletons;
  std::vector<UnsupportedRenderable> unsupported_renderables;
  // Degradation is separate from success: unsupported data remains in the
  // source Stage, approximations have render output, and failures omit output.
  std::vector<ConversionDiagnostic> conversion_diagnostics;
  PhysicsAnnotations physics;

  // Root nodes
  std::vector<int32_t> root_nodes;

  // Lookup by path
  std::unordered_map<std::string, int32_t> node_by_path;
  std::unordered_map<std::string, int32_t> mesh_by_path;
  std::unordered_map<std::string, int32_t> points_by_path;
  std::unordered_map<std::string, int32_t> curves_by_path;
  std::unordered_map<std::string, int32_t> point_instancer_by_path;
  std::unordered_map<std::string, int32_t> material_by_path;

  // Memory usage
  size_t memory_usage() const;

  // Bounds-checked accessors
  const RenderMesh* get_mesh(int32_t mesh_id) const;
  const RenderPoints* get_points(int32_t points_id) const;
  const RenderCurves* get_curves(int32_t curves_id) const;
  const RenderMaterial* get_material(int32_t material_id) const;
  const RenderPointInstancer* get_point_instancer(int32_t instancer_id) const;
  const RenderPointInstanceDraw* get_point_instance_draw(size_t draw_id) const;
  RenderPointInstanceDrawView get_point_instance_draw_view(size_t draw_id) const;
  RenderPointInstanceDrawRange get_point_instancer_draws(int32_t instancer_id) const;
  const SceneNode* get_node(int32_t node_id) const;
  bool has_valid_point_instance_draw_ranges() const;

  // Stats
  struct Stats {
    size_t node_count;
    size_t mesh_count;
    size_t points_count;
    size_t point_cloud_point_count;
    size_t curves_count;                    // RenderCurves records
    size_t curve_count;                     // individual curves (all records)
    size_t curve_tessellated_point_count;   // total tessellated polyline points
    size_t point_instancer_count;
    size_t point_instance_count;
    size_t visible_point_instance_count;
    size_t point_instance_draw_count;
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
}  // namespace lightusd
