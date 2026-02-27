# TinyUSDZ Tydra RenderScene Animation Extension — Implementation Specification

> **Scope**: Extend TinyUSDZ Tydra `render-data.hh` IR to support the full USD animation feature set and map it to Three.js `AnimationMixer`.
>
> **Target files**: `src/tydra/render-data.hh`, `src/tydra/scene-access.hh`, `src/tydra/render-data.cc`
>
> **Branch**: `skinning-phase1-fix1`

---

## 1. Overview

### 1.1 Goal

Map all USD animation sources through TinyUSDZ Tydra intermediate representation to Three.js AnimationMixer, covering:

| USD Feature | Status |
|---|---|
| SkelAnimation (joint TRS, blendShape weights) | **Implemented** (`ConvertSkelAnimation`) |
| xformOp timeSamples (node transform) | **Implemented** (`ExtractXformOpAnimation`), needs pattern classification |
| ValueClip (clip scheduling, time remapping) | **New** |
| Material parameter timeSamples | **New** |
| Vertex animation (points timeSamples) | **New** |
| Camera parameter timeSamples | **New** |
| Light parameter timeSamples | **New** |
| Visibility timeSamples | **New** |

### 1.2 Architecture

```
USD Stage
    │
    ▼
┌──────────────────────────────────────┐
│  TinyUSDZ Tydra RenderScene (C++)    │   ← This specification
│                                      │
│  AnimationClip[] ──── unified        │
│  VertexAnimationData ── per-mesh     │
│  ValueClipSchedule[] ── scheduling   │
│  XformOpDecompositionInfo[] ── diag  │
└──────────────────────────────────────┘
    │
    ▼  (WASM/JS binding or JSON export)
┌──────────────────────────────────────┐
│  Three.js Runtime                    │
│  AnimationMixer + AnimationAction    │
│  (+ optional ValueClipScheduler)     │
│  (+ optional VertexAnimationPlayer)  │
└──────────────────────────────────────┘
```

### 1.3 Design Principles

1. **Existing AnimationClip structure is preserved** — backward compatibility with glTF/Three.js.
2. **Unified channel model** — Material, Camera, Light, Visibility animations all go into `AnimationChannel` + `KeyframeSampler`, just like transforms. This mirrors the glTF `KHR_animation_pointer` philosophy.
3. **Vertex animation is stored separately** — Data volume is orders of magnitude larger than property animations; it lives in `RenderMesh`, not `AnimationClip`.
4. **ValueClip is a scheduling layer above AnimationClip** — It controls _when_ and _at what speed_ clips play, not what they contain.
5. **IR absorbs USD complexity** — xformOp stack decomposition, Euler-to-quaternion, pivot baking, clipTimes remapping all happen in the converter. Three.js receives normalized, ready-to-play data.
6. **Bake by default, schedule on request** — ValueClips are baked into stage-time keyframes by default. Runtime scheduling is opt-in for applications that need dynamic time control.

---

## 2. Enums

### 2.1 AnimationSourceType (new)

Insert after `AnimationInterpolation` enum (~line 821).

```cpp
/// Tracks the USD origin of an AnimationClip.
/// Used for debugging, export round-tripping, and source-aware processing.
enum class AnimationSourceType {
  Unknown,
  XformOp,              ///< From xformOp timeSamples on Xformable prims
  SkelAnimation,        ///< From UsdSkelAnimation prim
  ValueClipBaked,       ///< From ValueClip, baked (clipTimes remapping pre-applied)
  ValueClipUnbaked,     ///< From ValueClip, unbaked (clip-local time)
  BlendShape,           ///< From BlendShape weight animation
  MaterialProperty,     ///< From timeSampled material shader inputs
  CameraProperty,       ///< From timeSampled camera attributes
  LightProperty,        ///< From timeSampled light attributes
  VisibilityProperty,   ///< From timeSampled visibility attribute
  VertexMorphWeights,   ///< Auto-generated morph weights for vertex animation
  Mixed,                ///< Multiple source types combined in one clip
};
```

### 2.2 XformOpPattern (new)

```cpp
/// How the converter resolved a USD xformOp stack to TRS keyframes.
/// Diagnostic metadata only — output is always normalized TRS.
enum class XformOpPattern {
  Unknown,
  SimpleTRS,        ///< [T, R, S] subset — direct mapping, no matrix math
  PivotTRS,         ///< [T, T:pivot, R, !invert!T:pivot, S] — pivot baked into T
  ArbitraryStack,   ///< General case — per-sample matrix compose + decompose
  SingleMatrix,     ///< Single xformOp:transform (4x4) — per-sample decompose
};
```

### 2.3 ChannelTargetType (extend existing, ~line 832)

Replace the existing enum:

```cpp
enum class ChannelTargetType {
  // Existing
  SceneNode,        ///< Node transform (xformOp) → node.position/quaternion/scale
  SkeletonJoint,    ///< Skeleton joint (SkelAnimation) → bone TRS

  // New
  MaterialParam,    ///< Material shader parameter → material.opacity etc.
  CameraParam,      ///< Camera intrinsic → camera.fov etc.
  LightParam,       ///< Light parameter → light.intensity etc.
  MeshMorph,        ///< Mesh morph target weight → morphTargetInfluences
  NodeVisibility,   ///< Visibility → object.visible
};
```

### 2.4 AnimationPath (extend existing, ~line 840)

Replace the existing enum:

```cpp
enum class AnimationPath {
  // Existing — core transform
  Translation,       ///< vec3
  Rotation,          ///< quat (x,y,z,w)
  Scale,             ///< vec3
  Weights,           ///< float[] morph target weights

  // New — typed property animation
  FloatProperty,     ///< Single float (opacity, roughness, intensity, focalLength, …)
  Float3Property,    ///< vec3 / color3f (diffuseColor, emissiveColor, light color, …)
  Float4Property,    ///< vec4 / color4f
  BoolProperty,      ///< Boolean as float (0.0/1.0). Always InterpolateDiscrete.
};
```

### 2.5 VertexAnimationStrategy (new)

```cpp
/// How animated vertex positions are stored and consumed.
enum class VertexAnimationStrategy {
  /// Per-frame absolute positions. App interpolates + uploads to GPU.
  RawFrames,
  /// Delta from rest pose as morph targets. Weight animation auto-generated.
  MorphTargets,
  /// (Future) PCA-compressed basis set with weight animation.
  CompressedBasis,
};
```

---

## 3. Structs — ValueClip Scheduling

Insert after `AnimationClip` struct (~line 992).

### 3.1 ClipTimeRemap

```cpp
/// A single knot in ValueClip's clipTimes time-remapping function.
/// Between knots, linear interpolation determines the mapping.
///
/// Times are in seconds (converted from USD timeCodes using timeCodesPerSecond).
///
/// Example: clipTimes = [(0,0), (24,12), (48,0)]
///   stage=12 → clip=6, stage=36 → clip=6 (playing backwards)
struct ClipTimeRemap {
  float stage_time{0.0f};
  float clip_time{0.0f};
};
```

### 3.2 ValueClipEntry

```cpp
/// One clip asset's activation window + time remapping within a ValueClip schedule.
struct ValueClipEntry {
  int32_t animation_id{-1};  ///< Index into RenderScene::animations

  float stage_time_start{0.0f};  ///< Start of active window (seconds)
  float stage_time_end{0.0f};    ///< End of active window (seconds)

  /// Time remap knots from USD clipTimes (seconds).
  /// Empty = identity: clip_time = stage_time - stage_time_start.
  std::vector<ClipTimeRemap> time_remaps;

  /// Original USD clip asset path (for metadata/export).
  std::string clip_asset_path;

  /// Evaluate clip-local time. Piecewise-linear interpolation over time_remaps.
  float EvalClipTime(float stage_time) const;

  /// Check if active at given stage time.
  bool IsActiveAt(float stage_time) const {
    return stage_time >= stage_time_start && stage_time <= stage_time_end;
  }
};
```

**`EvalClipTime` implementation**: Clamp to remap range, then piecewise-linear interpolation between adjacent knots. If `time_remaps` is empty, return `stage_time - stage_time_start`.

### 3.3 ValueClipSchedule

```cpp
/// ValueClip schedule for a prim. Groups all clip entries for one target.
///
/// Two usage modes (controlled by RenderSceneConverterConfig::bake_value_clips):
///
///   Bake mode (default, recommended for Three.js):
///     Converter pre-evaluates all clips → single AnimationClip in stage time.
///     entries[] preserved as metadata. baked_animation_id → the baked clip.
///     Three.js: standard mixer.clipAction(clip).play().
///
///   Schedule mode:
///     Each clip → separate AnimationClip in clip-local time.
///     entries[].animation_id → individual clips. time_remaps[] for per-frame remap.
///     Three.js: custom scheduler sets action.time each frame.
struct ValueClipSchedule {
  std::string prim_name;
  std::string abs_path;

  std::vector<ValueClipEntry> entries;  ///< Chronological by stage_time_start

  bool is_baked{false};
  int32_t baked_animation_id{-1};  ///< Valid only when is_baked == true

  int32_t target_skeleton_id{-1};  ///< Index into skeletons (-1 if N/A)
  int32_t target_node_id{-1};      ///< Index into nodes (-1 if N/A)

  std::vector<size_t> FindActiveEntries(float stage_time) const;
  bool empty() const { return entries.empty(); }
};
```

---

## 4. Structs — xformOp Decomposition Diagnostic

Insert after ValueClipSchedule.

```cpp
/// Diagnostic record of xformOp stack decomposition.
/// NOT required for rendering — AnimationClip output is always normalized TRS.
/// Populated only when RenderSceneConverterConfig::store_xform_op_info == true.
struct XformOpDecompositionInfo {
  std::string abs_path;
  int32_t node_id{-1};
  int32_t animation_id{-1};

  XformOpPattern pattern{XformOpPattern::Unknown};

  /// Original xformOpOrder tokens for debugging/export.
  std::vector<std::string> xform_op_order;

  /// Pivot value (PivotTRS only).
  value::float3 pivot{0.0f, 0.0f, 0.0f};
  bool has_pivot{false};

  /// Original Euler order if applicable (e.g. "XYZ"). Empty if orient/matrix.
  std::string original_euler_order;

  /// Max shear magnitude detected (ArbitraryStack/SingleMatrix only).
  /// >0.01 typically indicates visible shear loss.
  float max_shear_magnitude{0.0f};

  size_t num_samples_evaluated{0};

  struct OpStatus {
    std::string op_name;
    bool is_animated{false};
    size_t num_samples{0};
  };
  std::vector<OpStatus> op_statuses;
};
```

---

## 5. Structs — Vertex Animation

Insert after `VertexAttribute` or after `ShapeTarget`.

### 5.1 VertexAnimationFrame

```cpp
/// A single frame of vertex position data.
struct VertexAnimationFrame {
  float time{0.0f};  ///< Seconds

  /// Vertex positions: flat [x0,y0,z0, x1,y1,z1, …], 3 floats per vertex.
  /// For MorphTargets strategy, stores DELTAS from rest pose.
  std::vector<float> positions;

  /// Optional per-vertex normals. Same layout. Empty if not computed.
  std::vector<float> normals;
};
```

### 5.2 VertexAnimationData

```cpp
/// Vertex animation from timeSampled `points` attribute.
/// Separate from BlendShape targets (different semantics: simulation/capture vs. artist shapes).
struct VertexAnimationData {
  VertexAnimationStrategy strategy{VertexAnimationStrategy::RawFrames};

  std::vector<VertexAnimationFrame> frames;  ///< Sorted by time

  /// MorphTargets strategy only:
  int32_t morph_weight_animation_id{-1};        ///< Auto-generated weight clip
  std::vector<std::string> morph_target_names;   ///< Parallel to frames[]

  uint32_t num_vertices{0};
  std::string source_attr_path;
  float source_fps{24.0f};

  bool empty() const { return frames.empty(); }
  size_t num_frames() const { return frames.size(); }
  std::pair<float, float> time_range() const;
  size_t estimate_memory_usage() const;
};
```

---

## 6. Structs — Property Name Constants

Insert as a new namespace block, before or after the struct definitions.

These provide a stable cross-renderer vocabulary for `AnimationChannel::property_name`.

```cpp
namespace material_property {
  // UsdPreviewSurface
  constexpr const char* kDiffuseColor       = "diffuseColor";        // vec3
  constexpr const char* kEmissiveColor      = "emissiveColor";       // vec3
  constexpr const char* kSpecularColor      = "specularColor";       // vec3
  constexpr const char* kMetallic           = "metallic";            // float
  constexpr const char* kRoughness          = "roughness";           // float
  constexpr const char* kOpacity            = "opacity";             // float
  constexpr const char* kIor                = "ior";                 // float
  constexpr const char* kClearcoat          = "clearcoat";           // float
  constexpr const char* kClearcoatRoughness = "clearcoatRoughness";  // float
  constexpr const char* kOpacityThreshold   = "opacityThreshold";    // float
  constexpr const char* kDisplacement       = "displacement";        // float
  constexpr const char* kOcclusion          = "occlusion";           // float

  // OpenPBR (prefixed to disambiguate from PreviewSurface)
  constexpr const char* kOPB_BaseWeight        = "opb.base_weight";         // float
  constexpr const char* kOPB_BaseColor         = "opb.base_color";          // vec3
  constexpr const char* kOPB_BaseRoughness     = "opb.base_roughness";      // float
  constexpr const char* kOPB_BaseMetalness     = "opb.base_metalness";      // float
  constexpr const char* kOPB_SpecularWeight    = "opb.specular_weight";     // float
  constexpr const char* kOPB_SpecularColor     = "opb.specular_color";      // vec3
  constexpr const char* kOPB_SpecularRoughness = "opb.specular_roughness";  // float
  constexpr const char* kOPB_SpecularIOR       = "opb.specular_ior";        // float
  constexpr const char* kOPB_CoatWeight        = "opb.coat_weight";         // float
  constexpr const char* kOPB_CoatRoughness     = "opb.coat_roughness";      // float
  constexpr const char* kOPB_EmissionLuminance = "opb.emission_luminance";  // float
  constexpr const char* kOPB_EmissionColor     = "opb.emission_color";      // vec3
}

namespace camera_property {
  constexpr const char* kFocalLength        = "focalLength";         // float [mm]
  constexpr const char* kHorizontalAperture = "horizontalAperture";  // float [mm]
  constexpr const char* kVerticalAperture   = "verticalAperture";    // float [mm]
  constexpr const char* kFocusDistance      = "focusDistance";        // float
  constexpr const char* kFStop              = "fStop";                // float
  constexpr const char* kNear               = "znear";               // float
  constexpr const char* kFar                = "zfar";                // float
}

namespace light_property {
  constexpr const char* kIntensity          = "intensity";           // float
  constexpr const char* kColor              = "color";               // vec3
  constexpr const char* kExposure           = "exposure";            // float
  constexpr const char* kColorTemperature   = "colorTemperature";    // float
  constexpr const char* kRadius             = "radius";              // float
  constexpr const char* kWidth              = "width";               // float
  constexpr const char* kHeight             = "height";              // float
  constexpr const char* kConeAngle          = "coneAngle";           // float
  constexpr const char* kConeSoftness       = "coneSoftness";        // float
}

namespace node_property {
  constexpr const char* kVisible            = "visible";             // bool as float
}
```

---

## 7. Modifications to Existing Structs

### 7.1 AnimationChannel (~line 899)

Add these fields to the existing struct:

```cpp
struct AnimationChannel {
  // --- existing fields (unchanged) ---
  AnimationPath path;
  ChannelTargetType target_type{ChannelTargetType::SceneNode};
  int32_t target_node{-1};
  int32_t skeleton_id{-1};
  int32_t joint_id{-1};
  int32_t sampler{-1};

  // --- new fields ---

  /// Target object index. Interpretation depends on target_type:
  ///   MaterialParam  → RenderScene::materials index
  ///   CameraParam    → RenderScene::cameras index
  ///   LightParam     → RenderScene::lights index
  ///   MeshMorph      → RenderScene::meshes index
  ///   NodeVisibility → same as target_node
  int32_t target_object{-1};

  /// Property name for property animation paths (FloatProperty, Float3Property, etc.).
  /// Uses standardized names from material_property:: / camera_property:: / etc.
  /// Empty for core TRS paths (Translation/Rotation/Scale/Weights).
  std::string property_name;

  // --- updated methods ---

  bool is_skeletal() const {
    return target_type == ChannelTargetType::SkeletonJoint;
  }

  bool is_property_animation() const {
    return target_type == ChannelTargetType::MaterialParam
        || target_type == ChannelTargetType::CameraParam
        || target_type == ChannelTargetType::LightParam;
  }
};
```

### 7.2 AnimationClip (~line 960)

Add these fields:

```cpp
struct AnimationClip {
  // --- existing fields (unchanged) ---
  std::string name;
  std::string prim_name;
  std::string abs_path;
  std::string display_name;
  float duration{0.0f};
  std::vector<KeyframeSampler> samplers;
  std::vector<AnimationChannel> channels;

  // --- new fields ---

  AnimationSourceType source_type{AnimationSourceType::Unknown};

  /// xformOp pattern used (valid when source_type == XformOp).
  XformOpPattern xform_op_pattern{XformOpPattern::Unknown};

  /// Original clip asset path (valid when source_type == ValueClip*).
  std::string source_clip_asset_path;

  /// True if matrix decomposition lost shear (ArbitraryStack/SingleMatrix).
  bool has_shear_loss{false};

  // --- existing methods (unchanged) ---
  bool empty() const;
  size_t num_channels() const;
  bool has_skeletal_animation() const;
  bool has_node_animation() const;

  // --- new query methods ---
  bool has_material_animation() const;
  bool has_camera_animation() const;
  bool has_light_animation() const;
  bool has_visibility_animation() const;
};
```

### 7.3 ShaderParam (~line 1515)

Add to the existing template class:

```cpp
template<typename T>
class ShaderParam {
 public:
  // --- existing ---
  T value{};
  int32_t texture_id{-1};

  // --- new ---

  /// True if this parameter has timeSamples in the USD source.
  /// Animation data is in RenderScene::animations as MaterialParam channel.
  /// The value field holds the static value at the conversion timecode.
  bool is_animated{false};

  /// Number of timeSamples (informational, 0 = not animated).
  uint32_t num_time_samples{0};
};
```

### 7.4 Node (~line 994)

Add:

```cpp
struct Node {
  // --- existing fields ---
  // ...
  bool has_resetXform{false};

  // --- new ---

  /// Visibility at evaluation timecode. true = "inherited", false = "invisible".
  bool visible{true};

  /// True if visibility has timeSamples. Animation in AnimationClip [NodeVisibility].
  bool visibility_is_animated{false};
};
```

### 7.5 RenderMesh (~line 1122)

Add after `std::map<std::string, ShapeTarget> targets;`:

```cpp
  /// Vertex animation from timeSampled `points` attribute.
  /// Separate from BlendShape targets (different semantics).
  nonstd::optional<VertexAnimationData> vertex_animation;

  /// True when points has timeSamples but normals does not.
  /// Application should recompute normals per frame.
  bool needs_dynamic_normals{false};

  bool has_vertex_animation() const {
    return vertex_animation.has_value() && !vertex_animation->empty();
  }
```

### 7.6 RenderCamera (~line 1731)

Add:

```cpp
  struct AnimatedFlags {
    bool focalLength{false};
    bool horizontalAperture{false};
    bool verticalAperture{false};
    bool focusDistance{false};
    bool fStop{false};
    bool clippingRange{false};
    bool any() const { return focalLength || horizontalAperture || verticalAperture || focusDistance || fStop || clippingRange; }
  };
  AnimatedFlags animated;
```

### 7.7 RenderLight (~line 1769)

Add:

```cpp
  struct AnimatedFlags {
    bool intensity{false};
    bool color{false};
    bool exposure{false};
    bool colorTemperature{false};
    bool radius{false};
    bool coneAngle{false};
    bool any() const { return intensity || color || exposure || colorTemperature || radius || coneAngle; }
  };
  AnimatedFlags animated;
```

### 7.8 SkelHierarchy (scene-access.hh ~line 599)

Add:

```cpp
  /// Index into RenderScene::value_clip_schedules (-1 = no ValueClip).
  int32_t value_clip_schedule_id{-1};

  /// Cached joint count.
  int32_t num_joints{0};

  /// Fast lookup: joint path → flat array index.
  std::map<std::string, int> joint_path_to_index;
```

### 7.9 RenderScene (~line 1876)

Add after `std::vector<BufferData> buffers;`:

```cpp
  /// ValueClip scheduling metadata.
  std::vector<ValueClipSchedule> value_clip_schedules;

  /// xformOp decomposition diagnostic records.
  /// Only populated when store_xform_op_info == true.
  std::vector<XformOpDecompositionInfo> xform_op_infos;
```

Also add:

```cpp
  /// Animation statistics.
  struct AnimationStats {
    size_t total_clips{0};
    size_t total_channels{0};
    size_t total_keyframes{0};
    size_t node_transform_channels{0};
    size_t skeletal_channels{0};
    size_t material_channels{0};
    size_t camera_channels{0};
    size_t light_channels{0};
    size_t visibility_channels{0};
    size_t morph_weight_channels{0};
    size_t vertex_animation_meshes{0};
    size_t vertex_animation_total_bytes{0};
    size_t total_animation_bytes{0};
  };
  AnimationStats ComputeAnimationStats() const;
```

---

## 8. RenderSceneConverterConfig Extensions (~line 2136)

Add after existing fields:

```cpp
  // ===== Animation extraction control =====

  bool extract_node_animations{true};
  bool extract_skel_animations{true};
  bool extract_blendshape_animations{true};
  bool extract_material_animations{true};
  bool extract_camera_animations{true};
  bool extract_light_animations{true};
  bool extract_visibility_animations{true};
  bool extract_vertex_animations{true};

  // ===== ValueClip =====

  /// Bake ValueClip scheduling into single AnimationClip per target (default).
  /// false = separate clips + schedule metadata for runtime control.
  bool bake_value_clips{true};

  /// Override stage time range for baking. nullopt = auto-detect.
  nonstd::optional<std::pair<double, double>> value_clip_time_range;

  /// Resample rate (Hz) for baked ValueClip. 0 = use original sample times.
  float value_clip_resample_rate{0.0f};

  // ===== xformOp =====

  bool store_xform_op_info{false};

  /// Warn when shear loss exceeds this threshold (ArbitraryStack).
  float shear_loss_warning_threshold{0.01f};

  // ===== Vertex animation =====

  VertexAnimationStrategy vertex_animation_strategy{VertexAnimationStrategy::RawFrames};

  /// Max morph targets for MorphTargets strategy (0 = unlimited).
  uint32_t max_vertex_morph_targets{0};

  /// Recompute normals per vertex animation frame.
  bool recompute_vertex_animation_normals{false};

  /// Memory budget per mesh for vertex animation (bytes, 0 = unlimited).
  size_t vertex_animation_memory_budget{0};
```

---

## 9. Converter Methods

### 9.1 New Public Methods on RenderSceneConverter

Add after `ExtractXformOpAnimation` (~line 3177):

```cpp
  /// Enhanced xformOp extraction with pattern classification + diagnostics.
  bool ExtractXformOpAnimationEx(
      const RenderSceneConverterEnv &env,
      const Path &abs_path,
      const std::string &prim_name,
      const Xformable &xformable,
      int32_t target_node_index,
      AnimationClip *anim_out,
      XformOpDecompositionInfo *info_out);  // nullable

  /// Process ValueClip metadata. Bake or schedule mode per config.
  bool ConvertValueClips(
      const RenderSceneConverterEnv &env,
      const Path &abs_path,
      const Prim &prim,
      int32_t target_skeleton_id,
      int32_t target_node_index,
      std::vector<AnimationClip> *clips_out,
      ValueClipSchedule *schedule_out);

  /// Extract animated material parameters → AnimationClip channels.
  bool ExtractMaterialAnimations(
      const RenderSceneConverterEnv &env,
      const Path &material_abs_path,
      int32_t material_index,
      AnimationClip *clip_out);

  /// Extract animated camera parameters → AnimationClip channels.
  bool ExtractCameraAnimations(
      const RenderSceneConverterEnv &env,
      const Path &camera_abs_path,
      int32_t camera_index,
      AnimationClip *clip_out);

  /// Extract animated light parameters → AnimationClip channels.
  bool ExtractLightAnimations(
      const RenderSceneConverterEnv &env,
      const Path &light_abs_path,
      int32_t light_index,
      AnimationClip *clip_out);

  /// Extract visibility animation → AnimationClip channel.
  /// USD "inherited" → 1.0, "invisible" → 0.0. Uses Step interpolation.
  bool ExtractVisibilityAnimation(
      const RenderSceneConverterEnv &env,
      const Path &prim_abs_path,
      int32_t node_index,
      AnimationClip *clip_out);

  /// Extract vertex animation from animated points.
  /// RawFrames: stores absolute positions in vertex_anim_out.
  /// MorphTargets: stores deltas + auto-generates weight clip in anim_clip_out.
  bool ExtractVertexAnimation(
      const RenderSceneConverterEnv &env,
      const Path &abs_path,
      int32_t mesh_index,
      const std::vector<vec3> &rest_points,
      VertexAnimationData *vertex_anim_out,
      AnimationClip *anim_clip_out);
```

### 9.2 New Private Helpers

```cpp
  /// Classify xformOp stack → pattern.
  static XformOpPattern ClassifyXformOpStack(
      const std::vector<XformOp> &xformOps);

  /// Merge time samples from multiple xformOps into sorted unique array.
  static std::vector<float> MergeXformOpTimeSamples(
      const std::vector<XformOp> &xformOps,
      double timeCodesPerSecond);

  /// Evaluate single xformOp at time → Matrix4.
  static value::matrix4d EvaluateXformOpAtTime(
      const XformOp &op, double time,
      value::TimeSampleInterpolationType interp);

  /// Resolve PivotTRS: T' = T + pivot - R*pivot.
  static bool ResolvePivotTRS(
      const std::vector<XformOp> &xformOps, double time,
      value::TimeSampleInterpolationType interp,
      value::float3 *out_t, value::float4 *out_r, value::float3 *out_s);

  /// Compose arbitrary stack → matrix → TRS decompose.
  static bool ComposeAndDecompose(
      const std::vector<XformOp> &xformOps, double time,
      value::TimeSampleInterpolationType interp,
      value::float3 *out_t, value::float4 *out_r, value::float3 *out_s,
      float *out_shear_magnitude);

  /// Load clip asset Stage for ValueClip.
  bool LoadClipAsset(
      const RenderSceneConverterEnv &env,
      const std::string &clip_asset_path,
      const std::string &manifest_path,
      Stage *clip_stage_out);

  /// Bake ValueClip schedule → single AnimationClip.
  bool BakeValueClipToAnimationClip(
      const RenderSceneConverterEnv &env,
      const ValueClipSchedule &schedule,
      const std::vector<Stage> &clip_stages,
      int32_t target_skeleton_id,
      int32_t target_node_index,
      AnimationClip *baked_out);

  /// Generic: Animatable<T> → KeyframeSampler.
  template <typename T>
  bool AnimatableToKeyframeSampler(
      const Animatable<T> &source,
      double timeCodesPerSecond,
      KeyframeSampler *sampler_out);

  /// Visibility token timeSamples → float (0.0/1.0) KeyframeSampler.
  bool ConvertVisibilityTimeSamples(
      const TypedTimeSamples<Visibility> &ts,
      double timeCodesPerSecond,
      KeyframeSampler *sampler_out);

  /// Auto-generate morph weight animation for vertex animation.
  bool BuildVertexAnimationMorphWeights(
      const VertexAnimationData &vertex_anim,
      int32_t mesh_index,
      AnimationClip *weight_clip_out);
```

---

## 10. xformOp Pattern Classification Algorithm

### 10.1 Classification Logic

```
Input: xformOpOrder token list + XformOp objects

1. If single op of type Transform → SingleMatrix
2. For each op, classify:
   - translate (no suffix) → T
   - translate:* → pivot
   - !invert! translate:* → inverse_pivot
   - rotateX/Y/Z, rotateXYZ/XZY/YXZ/YZX/ZXY/ZYX, orient → R
   - scale → S
   - anything else → other
3. If other exists → ArbitraryStack
4. If pivot AND inverse_pivot → PivotTRS
5. If only {T, R, S} subset → SimpleTRS
6. Otherwise → ArbitraryStack
```

### 10.2 Pattern Resolution

**Pattern A (SimpleTRS)**:
- Merge time sample keys from all ops.
- At each time: directly read T, R (convert Euler→quat), S.
- Missing ops → identity (T=0, R=identity, S=1).

**Pattern B (PivotTRS)**:
- Read pivot as constant vec3.
- At each time t: `T' = T(t) + pivot - rotate(pivot, R(t))`, `R' = R(t)`, `S' = S(t)`.
- Pivot is eliminated; output is pure TRS.

**Pattern C (ArbitraryStack / SingleMatrix)**:
- At each time t: compose all ops left-to-right as 4×4 matrices.
- Decompose result → T, R (quat), S.
- Detect shear: check if `M != T * R * S` recomposition (measure max component diff).
- If shear > threshold → `has_shear_loss = true`, emit warning.

### 10.3 Euler → Quaternion Conversion

USD Euler angles are in **degrees**. Conversion depends on rotation order:

```
"rotateXYZ" → Rz * Ry * Rx  (intrinsic)
"rotateXZY" → Ry * Rz * Rx
"rotateYXZ" → Rz * Rx * Ry
"rotateYZX" → Rx * Rz * Ry
"rotateZXY" → Ry * Rx * Rz
"rotateZYX" → Rx * Ry * Rz
```

Single-axis rotations (`rotateX`, `rotateY`, `rotateZ`) → axis-angle quaternion.

`orient` (quatf) → direct copy (USD and Three.js both use x,y,z,w order).

---

## 11. ValueClip Conversion Strategies

### 11.1 Bake Mode (`bake_value_clips = true`)

```
For each prim with ValueClip metadata:
  1. Parse clipSets / clipAssetPaths / clipTimes / clipActive / clipManifestAssetPath
  2. Determine stage time range:
     - Use config.value_clip_time_range if set
     - Else Stage metadata startTimeCode/endTimeCode
     - Else scan all clipTimes for min/max
  3. For each time sample in range (or at resample_rate intervals):
     a. Find active clip(s) via clipActive
     b. Evaluate clipTimes → clip-local time
     c. Load clip asset if not cached
     d. Evaluate animation data at clip-local time
  4. Produce single AnimationClip with stage-time keyframes
  5. Populate ValueClipSchedule with metadata (is_baked = true)
```

### 11.2 Schedule Mode (`bake_value_clips = false`)

```
For each prim with ValueClip metadata:
  1. Parse clip metadata (same as above)
  2. For each clip asset:
     a. Load clip asset
     b. Convert to AnimationClip in clip-local time
     c. Add to RenderScene::animations
  3. Populate ValueClipSchedule.entries with:
     - animation_id → per-clip AnimationClip index
     - stage_time_start / stage_time_end from clipActive
     - time_remaps from clipTimes
  4. Application uses ValueClipScheduler at runtime
```

---

## 12. Material Animation Conversion Flow

```
ConvertPreviewSurfaceShaderParam<T>(env, param_attr, param_name, dst_param)
  │
  ├─ param_attr.is_timesamples()?
  │   YES:
  │     1. dst_param.value = evaluate(env.timecode)     // static value (unchanged)
  │     2. dst_param.is_animated = true
  │     3. dst_param.num_time_samples = count
  │     4. Build KeyframeSampler:
  │          times[] = timeSample keys (÷ timeCodesPerSecond → seconds)
  │          values[] = flat float array
  │     5. Build AnimationChannel:
  │          target_type = MaterialParam
  │          target_object = material_index
  │          property_name = param_name  (e.g. "opacity")
  │          path = FloatProperty or Float3Property
  │          sampler = index into clip.samplers
  │     6. Append channel + sampler to AnimationClip
  │
  │   NO: dst_param.value = static value (existing behavior)
```

Same pattern applies to Camera and Light parameters.

---

## 13. Vertex Animation Conversion Flow

```
ExtractVertexAnimation(env, abs_path, mesh_index, rest_points, ...)
  │
  ├─ Get Animatable<std::vector<value::point3f>> from GeomMesh.points
  ├─ has_timesamples()?
  │   NO: return false (not animated)
  │   YES:
  │     ├─ For each timeSample (key, points_array):
  │     │    - Convert timeCode → seconds
  │     │    - Flatten points to float[] (3 × num_vertices)
  │     │    - Optional: recompute normals if config says so
  │     │    - Create VertexAnimationFrame {time, positions, normals}
  │     │
  │     ├─ if strategy == RawFrames:
  │     │    Store frames directly in vertex_anim_out
  │     │    Done.
  │     │
  │     └─ if strategy == MorphTargets:
  │          For each frame:
  │            delta[i] = frame.positions[i] - rest_points[i]
  │            Store delta as frame.positions
  │            Name = "vtxanim_frame_{idx}"
  │          Call BuildVertexAnimationMorphWeights():
  │            For frame[i] at time[i] and frame[i+1] at time[i+1]:
  │              weight_i(t) = 1 - frac, weight_{i+1}(t) = frac, others = 0
  │            Create NumberKeyframeTracks for morphTargetInfluences
  │          Set vertex_anim_out.morph_weight_animation_id
```

---

## 14. Three.js Mapping Reference

### 14.1 AnimationChannel → Three.js Track Name

| ChannelTargetType | Track Name Pattern | Track Class |
|---|---|---|
| SceneNode + Translation | `"{nodeName}.position"` | VectorKeyframeTrack |
| SceneNode + Rotation | `"{nodeName}.quaternion"` | QuaternionKeyframeTrack |
| SceneNode + Scale | `"{nodeName}.scale"` | VectorKeyframeTrack |
| SkeletonJoint + Translation | `"{boneName}.position"` | VectorKeyframeTrack |
| SkeletonJoint + Rotation | `"{boneName}.quaternion"` | QuaternionKeyframeTrack |
| SkeletonJoint + Scale | `"{boneName}.scale"` | VectorKeyframeTrack |
| MaterialParam + FloatProperty | `"{meshName}.material.{threejsProp}"` | NumberKeyframeTrack |
| MaterialParam + Float3Property | `"{meshName}.material.{threejsProp}"` | ColorKeyframeTrack |
| CameraParam + FloatProperty | `"{cameraName}.{threejsProp}"` | NumberKeyframeTrack |
| LightParam + FloatProperty | `"{lightName}.{threejsProp}"` | NumberKeyframeTrack |
| LightParam + Float3Property | `"{lightName}.{threejsProp}"` | ColorKeyframeTrack |
| NodeVisibility + BoolProperty | `"{nodeName}.visible"` | BooleanKeyframeTrack |
| MeshMorph + Weights | `"{meshName}.morphTargetInfluences[{name}]"` | NumberKeyframeTrack |

### 14.2 Material Property Name Mapping (Tydra → Three.js)

| Tydra property_name | Three.js MeshStandardMaterial | Notes |
|---|---|---|
| `"opacity"` | `material.opacity` | |
| `"roughness"` | `material.roughness` | |
| `"metallic"` | `material.metalness` | **Name differs** |
| `"ior"` | `material.ior` | MeshPhysicalMaterial |
| `"clearcoat"` | `material.clearcoat` | MeshPhysicalMaterial |
| `"clearcoatRoughness"` | `material.clearcoatRoughness` | MeshPhysicalMaterial |
| `"diffuseColor"` | `material.color` | THREE.Color |
| `"emissiveColor"` | `material.emissive` | THREE.Color |
| `"specularColor"` | `material.specularColor` | MeshPhysicalMaterial |

### 14.3 Camera: focalLength → fov Derived Property

Three.js uses `fov` (vertical FOV in degrees), but USD stores `focalLength` and `verticalAperture` as separate attributes that may be independently animated.

The Tydra IR stores the USD parameters as-is. The Three.js layer must compute the derived `fov` track:

```
fov(t) = 2 × atan( verticalAperture(t) / (2 × focalLength(t)) ) × (180/π)
```

When both parameters are animated, merge their time keys and evaluate both at each merged time.

### 14.4 Visibility: Token → Boolean

USD `visibility` is a token: `"inherited"` or `"invisible"`.

Conversion: `"inherited"` → `1.0`, `"invisible"` → `0.0`.

Three.js: `object.visible = (value > 0.5)`. Use `InterpolateDiscrete` (Step).

### 14.5 Vertex Animation Playback

**RawFrames strategy**:
- App finds 2 bracketing frames via binary search on `frames[].time`.
- Linearly interpolate: `pos[i] = A.positions[i] × (1-t) + B.positions[i] × t`.
- Upload to `geometry.attributes.position.array`, set `needsUpdate = true`.
- If `needs_dynamic_normals`: call `geometry.computeVertexNormals()`.

**MorphTargets strategy**:
- Standard Three.js morph pipeline.
- Auto-generated weight clip played through `AnimationMixer`.
- At any time, only 2 adjacent morph targets have non-zero weights.

### 14.6 ValueClip Runtime Scheduler (Schedule Mode)

```typescript
class ValueClipScheduler {
  setStageTime(stageTime: number): void {
    // 1. Disable all actions
    // 2. For each schedule entry active at stageTime:
    //      clipTime = entry.EvalClipTime(stageTime)
    //      action.enabled = true
    //      action.play()
    //      action.time = clipTime
    // 3. mixer.update(0)  // force evaluation at exact time
  }
}
```

---

## 15. Coordinate System and Unit Conversions

| Aspect | USD | Three.js | Action |
|---|---|---|---|
| Up axis | Y-up (default) | Y-up | No conversion needed |
| Quaternion order | (x,y,z,w) | (x,y,z,w) | No conversion needed |
| Euler angles | Degrees | Radians | Convert in Tydra (÷ 180 × π) |
| Translation units | metersPerUnit metadata | Unitless | Multiply by metersPerUnit in Tydra |
| Time units | timeCodes | Seconds | Divide by timeCodesPerSecond in Tydra |

All conversions happen in the Tydra converter. The output AnimationClip data is always in seconds, radians (already converted to quaternions), and scaled units.

---

## 16. AnimationClip Organization Strategy

### Which channels go into which clip?

| Source | Clip Strategy |
|---|---|
| SkelAnimation | One clip per SkelAnimation prim (channels = all joints) |
| xformOp per prim | Merge into scene-level clip OR one clip per prim |
| Material/Camera/Light/Visibility | Merge into a single scene-level "property_animation" clip |
| Vertex animation (MorphTargets) | One weight clip per mesh |
| ValueClip (baked) | One baked clip per ValueClip prim |
| ValueClip (unbaked) | One clip per clip asset file |

For the scene-level clips, all channels sharing the same stage time range (startTimeCode–endTimeCode) are combined into one `AnimationClip`. This minimizes the number of `AnimationAction` objects in Three.js.

---

## 17. Memory Considerations

### Vertex Animation Budget

```
10K verts × 120 frames ≈  14 MB   — acceptable for web
50K verts × 240 frames ≈ 144 MB   — needs budget limit
100K verts × 480 frames ≈ 576 MB  — requires CompressedBasis
```

When `vertex_animation_memory_budget` is exceeded, uniformly subsample frames to fit.

### Material/Camera/Light Animation

Typically small (tens to hundreds of keyframes, a few floats each). No special memory management needed.

### AnimationClip Sampler Data

`KeyframeSampler.values` is a flat float array. For 1000 keyframes × vec3 = 12 KB per sampler. Negligible even with hundreds of channels.

---

## 18. Implementation Priority

### Phase 1: Transform Animation

| Step | Description | Difficulty |
|---|---|---|
| 1.1 | `ClassifyXformOpStack()` — pattern detection | Low |
| 1.2 | `ExtractXformOpAnimationEx()` — Pattern A/B/C resolution | Medium |
| 1.3 | `XformOpDecompositionInfo` — diagnostic record | Low |
| 1.4 | ValueClip structs (`ValueClipSchedule`, `ClipTimeRemap`, `ValueClipEntry`) | Low |
| 1.5 | `ConvertValueClips()` — bake mode | High |
| 1.6 | `ConvertValueClips()` — schedule mode | Medium |

### Phase 2a: Property Animation

| Step | Description | Difficulty |
|---|---|---|
| 2.1 | `AnimationPath` / `ChannelTargetType` enum extensions | Low |
| 2.2 | `AnimationChannel.target_object` / `property_name` fields | Low |
| 2.3 | `ExtractVisibilityAnimation()` — simplest property | Low |
| 2.4 | `ExtractMaterialAnimations()` — float scalars | Medium |
| 2.5 | `ExtractMaterialAnimations()` — vec3 colors | Medium |
| 2.6 | `ExtractLightAnimations()` | Low |
| 2.7 | `ExtractCameraAnimations()` — store raw, note fov derivation | Medium |

### Phase 2b: Vertex Animation

| Step | Description | Difficulty |
|---|---|---|
| 2.8 | `VertexAnimationData` / `VertexAnimationFrame` structs | Low |
| 2.9 | `ExtractVertexAnimation()` — RawFrames mode | Medium |
| 2.10 | `BuildVertexAnimationMorphWeights()` — MorphTargets mode | High |
| 2.11 | Normal recomputation per frame | Medium |
| 2.12 | Memory budget enforcement + frame subsampling | Medium |

### Phase 3: Polish

| Step | Description |
|---|---|
| 3.1 | OpenPBR material animation (same framework, different param names) |
| 3.2 | `AnimationStats` + `ComputeAnimationStats()` |
| 3.3 | CompressedBasis vertex animation (PCA) |
| 3.4 | ValueClip × property animation combination testing |
| 3.5 | Integration tests with USD sample files |

---

## 19. Appendix — Migration from Per-Node Animation

> Merged from `doc/ANIMATION_SYSTEM_REDESIGN.md` (now deprecated).

### 19.1 Migration Path

The animation system was redesigned from a per-node embedded model to an independent clip-based model compatible with glTF / Three.js.

**Old System (USD-centric, per-node):**
```cpp
// Animation embedded in each node
node.node_animations[i].type = AnimationChannel::ChannelType::Translation;
node.node_animations[i].translations.samples[j].t = time;
node.node_animations[i].translations.samples[j].value = position;
```

**New System (glTF/Three.js compatible, clip-based):**
```cpp
// Animation as independent clips
AnimationClip clip;
clip.name = "Walk";

// Create sampler
KeyframeSampler sampler;
sampler.times = {0.0f, 1.0f, 2.0f};
sampler.values = {0,0,0,  1,0,0,  0,0,0};  // Flat array
sampler.interpolation = AnimationInterpolation::Linear;
clip.samplers.push_back(sampler);

// Create channel linking sampler to node property
AnimationChannel channel;
channel.path = AnimationPath::Translation;
channel.target_node = 5;  // Index into RenderScene::nodes
channel.sampler = 0;       // Index into clip.samplers
clip.channels.push_back(channel);

// Add to scene
scene.animations.push_back(clip);
```

### 19.2 Benefits

1. **Three.js Compatibility**: Direct mapping to Three.js AnimationClip / KeyframeTrack
2. **glTF Compatible**: Matches glTF 2.0 animation structure
3. **Separation of Concerns**: Animations independent from scene hierarchy
4. **Memory Efficient**: Flat array storage reduces overhead
5. **Flexible**: Multiple channels can target the same node with different samplers

### 19.3 Three.js Export Quick-Start

```javascript
// In Three.js / JavaScript
const times = new Float32Array(samplers[0].times);
const values = new Float32Array(samplers[0].values);

// Create KeyframeTrack based on path
let track;
switch (channel.path) {
  case 'Translation':
    track = new THREE.VectorKeyframeTrack('.position', times, values);
    break;
  case 'Rotation':
    track = new THREE.QuaternionKeyframeTrack('.quaternion', times, values);
    break;
  case 'Scale':
    track = new THREE.VectorKeyframeTrack('.scale', times, values);
    break;
}

// Create AnimationClip
const clip = new THREE.AnimationClip(name, duration, [track]);
```

### 19.4 Important Notes

1. **Rotations use Quaternions**: Not Euler angles (matches Three.js requirement)
2. **Times in Seconds**: All animation times are in seconds (float)
3. **Flat Arrays**: Values stored as flat float arrays for efficiency
4. **Node Indexing**: Channels reference nodes by index, not path
5. **Backward Compatibility**: Old `AnimationSampler<T>` template still exists for legacy USD data

---

## 20. Blender Multi-Clip Animation Export

### 20.1 Problem

Blender's built-in USD exporter only exports the **single active Action** as one `UsdSkelAnimation`. There is no native support for exporting multiple animation clips from a single Blender scene.

### 20.2 Solution: TinyUSDZ Animation Clips Exporter (Blender Addon)

A single-file Blender addon (`web/js/scripts/tinyusdz_anim_clips.py`, v1.1) provides two export workflows. See the addon's own [README](../web/js/scripts/README.md) for full installation and usage instructions.

**Requirements**: Blender 4.0+ (tested on 5.0.1) with the built-in `pxr` Python package.

### 20.3 USDHook Mechanism

Blender 4.0+ exposes `bpy.types.USDHook`. The addon subclasses it and registers via `bpy.utils.register_class()`. Once registered, the hook fires **automatically** on every USD export.

```
File > Export > USD
    │
    ▼
Blender builds the USD stage (scene graph, geometry, materials, active-action animation)
    │
    ▼
Blender calls hook.on_export(export_context) for each registered USDHook
    │
    ├─ export_context.get_stage()  → Usd.Stage (read-write)
    ├─ bpy.context / bpy.data      → full Blender access
    │
    ▼
Hook writes custom attributes to the stage
    │
    ▼
Blender serialises the stage to disk (.usda / .usdc)
```

Key points:

- `on_export()` fires **after** Blender converts the entire scene but **before** the file is written to disk. The hook receives a fully-populated `Usd.Stage` that it can modify in-place.
- The hook has **full access to `bpy.data`** (actions, NLA tracks, objects, armatures) alongside the USD stage, enabling enumeration of all Actions.
- If `on_export()` returns `False`, the export is aborted.

### 20.4 Clip Discovery

The addon discovers clips by scanning **all objects** in `bpy.data.objects`:

| Source | How found |
|--------|-----------|
| Active Action | `obj.animation_data.action` |
| NLA strip Actions | Each strip's `.action` in every NLA track on the object |

Actions are deduplicated per object. Supported object types:

| Blender Type | USD Source Type | Animated data |
|-------------|----------------|---------------|
| `ARMATURE` | `SkelAnimation` | Joint rotations, translations, scales |
| `CAMERA` | `XformOp` | Location, rotation (`xformOp:translate`, `xformOp:rotateXYZ`) |
| `LIGHT` | `XformOp` | Location, rotation; energy/color via `inputs:intensity.timeSamples` |
| `MESH` | `XformOp` | Object-level translate, rotate, scale |
| `EMPTY` | `XformOp` | Transform animation |

### 20.5 Workflow 1 — Option C: Concatenated Timeline + Metadata (USD Hook)

**File > Export > USD** with Animation enabled. The hook fires, discovers all Actions, and writes metadata. No additional steps required.

#### Metadata format (v1)

Attributes are written to the **first non-generated child prim** of the default prim. Blender's generated root Xform (tagged with `customData.Blender.generated=1`) is stripped on re-import, so writing there would lose the data.

All multi-value fields use **`|` (pipe)** as separator. Full attribute prefix: `userProperties:tinyusdz:animClips:`

```
custom int    userProperties:tinyusdz:animClips:version         # 1
custom int    userProperties:tinyusdz:animClips:clipCount       # 3
custom float  userProperties:tinyusdz:animClips:fps             # 24.0
custom string userProperties:tinyusdz:animClips:names           # "Walk|Idle|CubeMove"
custom string userProperties:tinyusdz:animClips:startFrames     # "1|1|1"
custom string userProperties:tinyusdz:animClips:endFrames       # "72|48|60"
custom string userProperties:tinyusdz:animClips:sourceTypes     # "SkelAnimation|SkelAnimation|XformOp"
custom string userProperties:tinyusdz:animClips:objectNames     # "CharacterRig|CharacterRig|Cube"
custom string userProperties:tinyusdz:animClips:objectTypes     # "Armature|Armature|Mesh"
```

On Blender re-import with `property_import_mode='USER'` (or `'ALL'`), the `userProperties:` prefix is stripped, leaving `tinyusdz:animClips:*` as Blender custom properties.

#### Auto-Bake mode

Concatenates all Actions end-to-end via a temporary NLA setup, exports the full timeline, and additionally writes:

```
custom string userProperties:tinyusdz:animClips:concatStartFrames  # "0|25|73"
custom string userProperties:tinyusdz:animClips:concatEndFrames    # "24|72|108"
```

#### Design decisions

| Decision | Rationale |
|----------|-----------|
| Scalar strings, not arrays | Blender's USD importer silently drops `string[]` and `int[]` custom attributes. Only scalar types (`string`, `int`, `float`) survive the round-trip. |
| `\|` separator, not `,` | Action names like `"Walk, Fast"` would break CSV parsing. |
| `userProperties:tinyusdz:` namespace | `userProperties:` is the Blender-recognized namespace for custom properties. `tinyusdz:` sub-namespace avoids collision with other addons. |
| Write to first child, not default prim | Blender tags its generated root Xform with `customData.Blender.generated=1` and strips it on re-import. Writing to the first real child ensures survival. |

### 20.6 Workflow 2 — Separate Files (HumanFemale Pattern)

Exports one USD file per Action, following the Pixar `UsdSkelExamples/HumanFemale/` convention:

```
output/
  Scene.usda               # Base: geometry + skeleton (no animation)
  Scene.Idle.usda           # Animation only (prepend references = @./Scene.usda@)
  Scene.Walk.usda           # Animation only (prepend references = @./Scene.usda@)
  Scene.CamOrbit.usda       # Animation only (prepend references = @./Scene.usda@)
```

The operator: (1) exports the base scene with `export_animation=False`, (2) for each Action sets it as active, adjusts frame range, exports with animation, and post-processes to add `prepend references`, (3) restores original scene state.

### 20.7 Consumer-Side Integration

#### JavaScript / TinyUSDZ Viewer

```javascript
const SEP = '|';
const NS = 'userProperties:tinyusdz:animClips:';
const clipCount = prim.getIntAttribute(NS + 'clipCount');
const fps       = prim.getFloatAttribute(NS + 'fps');
const names       = prim.getAttribute(NS + 'names').split(SEP);
const startFrames = prim.getAttribute(NS + 'startFrames').split(SEP).map(Number);
const endFrames   = prim.getAttribute(NS + 'endFrames').split(SEP).map(Number);
const sourceTypes = prim.getAttribute(NS + 'sourceTypes').split(SEP);

for (let i = 0; i < clipCount; i++) {
    const subClip = THREE.AnimationUtils.subclip(
        fullClip, names[i], startFrames[i], endFrames[i], fps
    );
    mixer.clipAction(subClip);
}
```

#### TinyUSDZ C++ (`render-data.cc`) — Future TODO

1. Read `userProperties:tinyusdz:animClips:*` attributes from the first child prim
2. Parse `|`-separated strings into vectors
3. Split the concatenated timeline into multiple `AnimationClip` objects by frame ranges
4. Tag each with `sourceType` / `objectType`
5. Expose via `RenderScene::animation_clips`

This is **not yet implemented**.
