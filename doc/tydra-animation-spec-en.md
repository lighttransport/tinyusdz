# TinyUSDZ Tydra Animation

How USD animation is converted through the Tydra RenderScene IR and mapped to
Three.js. This document describes **what is implemented today** and, in a
separate section at the end, **what is still proposed**.

> Source files: `src/tydra/render-data.hh` (IR structs/enums),
> `src/tydra/render-data-anim.cc` and `src/tydra/render-animation-converter.cc`
> (converters), `src/tydra/threejs-exporter.cc` (Three.js export).

---

## 1. Implemented

### 1.1 Animation sources

| USD source | Converter | Notes |
|---|---|---|
| `UsdSkelAnimation` (joint TRS + blendShape weights) | `ConvertSkelAnimation` | One clip per SkelAnimation prim. |
| `xformOp` timeSamples on Xformable prims | `ExtractXformOpAnimation` | Per animated op; `xformOp:transform` matrices are decomposed to TRS. |
| USD value clips | `ConvertValueClipAnimation` | Loads clip layers, resolves the active clip per stage time, **bakes** to TRS + generic property tracks. |

All three produce `AnimationClip` objects stored in `RenderScene::animations`.

### 1.2 IR data model (`render-data.hh`)

```cpp
enum class AnimationSourceType { Unknown, XformOp, SkelAnimation, BlendShape };

enum class ChannelTargetType { SceneNode, SkeletonJoint };

enum class AnimationPath {
  Translation,    // vec3   -> Three.js .position
  Rotation,       // quat   -> Three.js .quaternion
  Scale,          // vec3   -> Three.js .scale
  Weights,        // float[] morph target weights
  CustomProperty, // arbitrary numeric prim property (name in property_name)
};

enum class AnimationInterpolation { Linear, Step, CubicSpline };
```

`KeyframeSampler` holds flat `times` (seconds) + `values` arrays and an
`AnimationInterpolation`. `AnimationChannel` binds a sampler to a target:
`SceneNode` channels use `target_node` (index into `RenderScene::nodes`);
`SkeletonJoint` channels use `skeleton_id` + `joint_id`. `CustomProperty`
channels carry `is_custom_property` + `property_name`.

`AnimationClip` carries `name`, `prim_name`, `abs_path`, `duration`,
`source_type`, the value-clip baking fields (`has_value_clip`,
`value_clip_baked`, `value_clip_start_time`/`value_clip_end_time`,
`value_clip_sample_rate`, `clip_asset_paths`), `num_animated_joints`/
`num_animated_nodes`, and the `samplers`/`channels` arrays.

> Animations reference scene nodes **by index**, not by embedding into the node
> hierarchy. This mirrors the glTF / Three.js design and is the result of an
> earlier redesign away from a per-node embedded model (Appendix A).

### 1.3 Value-clip handling

`ConvertValueClipAnimation` is **bake-only**: there is no runtime "schedule"
mode and no `ValueClipSchedule`/`ClipTimeRemap` structs. Per prim it:

1. Parses clip metadata (clip sets, asset paths, `clipTimes`, `clipActive`,
   manifest), with a small LRU cache of loaded clip layers/stages
   (`LoadValueClipLayer` / `LoadValueClipStage`, `kMaxValueClipCacheEntries`).
2. Determines the stage time range from `value_clip_start_time/end_time` (when
   `value_clip_use_time_range` is set) or auto-detects it; optionally resamples
   at `value_clip_sample_rate`.
3. At each sampled time, resolves the active clip, evaluates the data at the
   remapped clip-local time, and accumulates TRS tracks (from
   `xformOp:transform` matrices) plus generic `CustomProperty` tracks for any
   other animated attributes.
4. Emits one baked `AnimationClip` with `value_clip_baked = true`.

Relevant `RenderSceneConverterConfig` fields: `enable_value_clips`,
`value_clip_sample_rate`, `value_clip_use_time_range`,
`value_clip_start_time`, `value_clip_end_time`.

### 1.4 xformOp extraction

`ExtractXformOpAnimation` walks the xformOp stack and, for each op that has time
samples, emits a channel:

* `translate` -> Translation, `scale` -> Scale.
* `orient` (quatf) -> Rotation, copied directly (USD and Three.js both use
  x,y,z,w).
* `rotateX/Y/Z` and the six Euler `rotate{XYZ,XZY,YXZ,YZX,ZXY,ZYX}` orders ->
  Rotation (converted to quaternion).
* `xformOp:transform` (4x4 matrix) -> decomposed into Translation + Rotation +
  Scale channels.

There is **no** separate pattern-classification pass and **no** pivot/shear
diagnostic record; the output is normalized TRS regardless.

### 1.5 Three.js export

`ThreeJSSceneExporter::ConvertAnimation` turns each `AnimationClip` into a
tracks array. Target names are index-based, not prim-name-based:

| Channel | Track name | Track type |
|---|---|---|
| SceneNode | `node_{target_node}` | — |
| SkeletonJoint | `skel_{skeleton_id}_joint_{joint_id}` | — |
| `+ Translation` | `…​.position` | `vector3` |
| `+ Rotation` | `…​.quaternion` | `quaternion` |
| `+ Scale` | `…​.scale` | `vector3` |
| `+ Weights` | `…​.morphTargetInfluences` | `number` |
| `+ CustomProperty` | `…​.{property_name}` | `number` / `vector2/3/4` (by component count) |

Material, camera, light, and visibility animation have **no** dedicated track
mapping in the exporter; they are only representable through the generic
`CustomProperty` path.

### 1.6 Unit / coordinate conversions

All conversions happen in the converter, so emitted clips are normalized:

| Aspect | USD | Three.js | Action in Tydra |
|---|---|---|---|
| Quaternion order | (x,y,z,w) | (x,y,z,w) | none |
| Euler angles | degrees | radians (quaternion) | convert Euler -> quaternion |
| Time | timeCodes | seconds | divide by `timeCodesPerSecond` |
| Translation units | `metersPerUnit` | unitless | scale by `metersPerUnit` |

---

## 2. Proposed (NOT implemented)

The following were designed as a future "full USD animation feature set" but are
**not present in the source**. They are recorded here as a roadmap; none of the
enums, structs, fields, or methods below exist yet.

### 2.1 Additional animation sources

Dedicated extraction for material-parameter, camera-parameter,
light-parameter, visibility, and vertex (`points` timeSamples) animation. Today
these are only reachable via the generic `CustomProperty` mechanism inside value
clips.

### 2.2 IR extensions

* `ChannelTargetType` additions: `MaterialParam`, `CameraParam`, `LightParam`,
  `MeshMorph`, `NodeVisibility`.
* Typed property paths: `FloatProperty`, `Float3Property`, `Float4Property`,
  `BoolProperty` (replacing/augmenting the single `CustomProperty`).
* `AnimationChannel::target_object` (index into materials/cameras/lights/meshes)
  and typed query helpers (`is_property_animation()`).
* `AnimationClip` query helpers (`has_material_animation()`, etc.) and a
  `has_shear_loss` flag.
* `XformOpPattern` enum + `XformOpDecompositionInfo` diagnostic record (pivot,
  Euler order, shear magnitude, per-op status).
* Per-object animated flags on `ShaderParam`, `Node` (`visible`/
  `visibility_is_animated`), `RenderCamera`, `RenderLight`.
* Standardized property-name namespaces (`material_property::`,
  `camera_property::`, `light_property::`, `node_property::`).
* `RenderScene::AnimationStats` + `ComputeAnimationStats()`.

### 2.3 Value-clip schedule mode

A non-baking mode that emits one clip per clip asset plus a
`ValueClipSchedule` / `ValueClipEntry` / `ClipTimeRemap` metadata layer, driven
at runtime by a JS `ValueClipScheduler` (set `action.time` per frame). Only bake
mode exists today.

### 2.4 Vertex animation

`VertexAnimationData` / `VertexAnimationFrame` on `RenderMesh`, with `RawFrames`
and `MorphTargets` strategies (auto-generated morph-weight clips), per-frame
normal recomputation, and a memory budget with frame subsampling.

### 2.5 Derived Three.js properties

* Camera `fov` derived from animated `focalLength` + `verticalAperture`:
  `fov(t) = 2·atan(verticalAperture(t) / (2·focalLength(t)))·(180/π)`.
* Visibility token -> boolean: `inherited`→1.0, `invisible`→0.0, Step interp,
  `object.visible = (value > 0.5)`.
* Material property name mapping (e.g. `metallic` -> `material.metalness`,
  `diffuseColor` -> `material.color`).

---

## Appendix A — Migration from per-node animation

The animation system was redesigned from a per-node embedded model to the
independent clip-based model above (compatible with glTF / Three.js).

**Old (per-node):**
```cpp
node.node_animations[i].type = ...Translation;
node.node_animations[i].translations.samples[j].t = time;
```

**New (clip-based):**
```cpp
AnimationClip clip;
clip.name = "Walk";
KeyframeSampler sampler;
sampler.times  = {0.0f, 1.0f, 2.0f};
sampler.values = {0,0,0,  1,0,0,  0,0,0};   // flat
clip.samplers.push_back(sampler);

AnimationChannel channel;
channel.path = AnimationPath::Translation;
channel.target_node = 5;   // index into RenderScene::nodes
channel.sampler = 0;
clip.channels.push_back(channel);
scene.animations.push_back(clip);
```

Key invariants: rotations are quaternions; times are seconds; values are flat
float arrays; channels reference nodes by index; the legacy
`AnimationSampler<T>` template still exists for raw USD data.

---

## Appendix B — Blender multi-clip export addon

Blender's built-in USD exporter only exports the single active Action as one
`UsdSkelAnimation`. The single-file addon
`web/js/scripts/tinyusdz_anim_clips.py` (v1.1, Blender 4.0+) adds multi-clip
export via two workflows. See the addon's
[README](../web/js/scripts/README.md) for installation and usage.

### B.1 USDHook

The addon subclasses `bpy.types.USDHook` (Blender 4.0+) and registers via
`bpy.utils.register_class()`. `on_export()` fires automatically after Blender
builds the stage but before it is written, receiving a read-write `Usd.Stage`
plus full `bpy.data` access (Actions, NLA tracks). Returning `False` aborts the
export.

### B.2 Clip discovery

Scans `bpy.data.objects` for the active Action (`obj.animation_data.action`)
and NLA strip Actions, deduplicated per object. Supported types: `ARMATURE`
(SkelAnimation), `CAMERA`/`LIGHT`/`MESH`/`EMPTY` (XformOp; lights also write
`inputs:intensity.timeSamples`).

### B.3 Workflow 1 — concatenated timeline + metadata

`File > Export > USD` with Animation enabled. The hook writes
`|`-separated metadata to the first non-generated child of the default prim
(Blender strips its generated root Xform, tagged
`customData.Blender.generated=1`, on re-import):

```
custom int    userProperties:tinyusdz:animClips:version       # 1
custom int    userProperties:tinyusdz:animClips:clipCount     # 3
custom float  userProperties:tinyusdz:animClips:fps           # 24.0
custom string userProperties:tinyusdz:animClips:names         # "Walk|Idle|CubeMove"
custom string userProperties:tinyusdz:animClips:startFrames   # "1|1|1"
custom string userProperties:tinyusdz:animClips:endFrames     # "72|48|60"
custom string userProperties:tinyusdz:animClips:sourceTypes   # "SkelAnimation|SkelAnimation|XformOp"
custom string userProperties:tinyusdz:animClips:objectNames   # "CharacterRig|CharacterRig|Cube"
custom string userProperties:tinyusdz:animClips:objectTypes   # "Armature|Armature|Mesh"
```

Auto-Bake mode additionally concatenates Actions via a temporary NLA setup and
writes `concatStartFrames` / `concatEndFrames`.

Design constraints: scalar strings only (Blender's importer drops `string[]`/
`int[]` custom attrs); `|` separator (commas appear in Action names);
`userProperties:tinyusdz:` namespace (recognized by Blender, collision-free).

### B.4 Workflow 2 — separate files (HumanFemale pattern)

One USD file per Action following Pixar's `UsdSkelExamples/HumanFemale/`
convention: a base `Scene.usda` (geometry + skeleton, no animation) plus
`Scene.<Action>.usda` files that `prepend references = @./Scene.usda@` and carry
animation only.

### B.5 Consumer side

JavaScript splits the concatenated clip with `THREE.AnimationUtils.subclip`:

```javascript
const SEP = '|', NS = 'userProperties:tinyusdz:animClips:';
const clipCount = prim.getIntAttribute(NS + 'clipCount');
const fps       = prim.getFloatAttribute(NS + 'fps');
const names       = prim.getAttribute(NS + 'names').split(SEP);
const startFrames = prim.getAttribute(NS + 'startFrames').split(SEP).map(Number);
const endFrames   = prim.getAttribute(NS + 'endFrames').split(SEP).map(Number);
for (let i = 0; i < clipCount; i++) {
  mixer.clipAction(
    THREE.AnimationUtils.subclip(fullClip, names[i], startFrames[i], endFrames[i], fps));
}
```

C++ (`render-data.cc`) parsing of these `animClips:*` attributes into multiple
`AnimationClip`s is **not yet implemented**.
