# USD Skeletal Animation API for Three.js

API reference for the TinyUSDZ WASM bindings and Three.js pipeline modules used in the skeletal animation viewer.

## Table of Contents

1. [WASM Binding API](#wasm-binding-api)
2. [Pipeline Modules](#pipeline-modules)
3. [Quick Start Example](#quick-start-example)
4. [Data Structures](#data-structures)
5. [Extended Skinning](#extended-skinning)
6. [Troubleshooting](#troubleshooting)

---

## WASM Binding API

The TinyUSDZ WASM binding (`TinyUSDZLoaderNative`) provides low-level access to USD skeleton, mesh, and animation data. All typed arrays returned are views into WASM heap memory -- **copy them before calling `usd_scene.delete()`** (see `docs/WASM_TYPED_MEMORY_VIEW.md`).

### Skeleton Methods

#### `numSkeletons(): number`

Returns the total number of skeletons in the loaded USD scene.

#### `getSkeleton(skelId: number): Object`

Retrieves complete skeleton hierarchy for a given skeleton ID.

**Returns:**
```javascript
{
  id: number,                // Skeleton ID
  prim_name: string,         // USD prim name (e.g., "Armature")
  abs_path: string,          // Absolute USD path
  display_name: string,      // Display name
  anim_id: number,           // Primary animation ID (-1 if none)
  anim_ids: number[],        // All animation IDs (multi-animation support)
  root_node: {               // Root joint node (recursive)
    joint_path: string,      // USD joint path
    joint_name: string,      // Joint name
    joint_id: number,        // Joint index
    bind_transform: Float64Array,  // 4x4 column-major matrix (16 doubles)
    rest_transform: Float64Array,  // 4x4 column-major matrix (16 doubles)
    children: [...],         // Child joints (same structure)
  }
}
```

#### `getAllSkeletons(): Array<Object>`

Returns all skeletons in the scene.

#### `getSkeletonJointsFlat(skelId: number): Object`

Returns skeleton data in a flattened format.

**Returns:**
```javascript
{
  num_joints: number,
  joint_names: string[],
  joint_paths: string[],
  joint_ids: Int32Array,
  parent_indices: Int32Array,     // -1 for root
  bind_matrices: Float64Array,    // num_joints * 16 (flattened 4x4 matrices)
  rest_matrices: Float64Array,    // num_joints * 16
}
```

### Animation Methods

#### `numAnimations(): number`

Returns the number of animation clips.

#### `getAnimation(animId: number): Object`

Returns full animation clip data including channels and samplers.

**Returns:**
```javascript
{
  id: number,
  name: string,
  prim_name: string,
  abs_path: string,
  duration: number,           // Duration in seconds
  channels: [{
    sampler: number,          // Index into samplers array
    target_node: number,      // DFS node index (SceneNode animations)
    skeleton_id: number,      // Skeleton ID (SkeletonJoint animations)
    joint_id: number,         // Joint ID within skeleton
    target_type: string,      // "SceneNode" or "SkeletonJoint"
    path: string,             // "Translation", "Rotation", "Scale", or "Weights"
  }],
  samplers: [{
    times: Float32Array,      // Keyframe times (in seconds, divided by timeCodesPerSecond)
    values: Float32Array,     // Flattened keyframe values
    interpolation: string,    // "LINEAR", "STEP", or "CUBICSPLINE"
  }]
}
```

#### `getAnimationInfo(animId: number): Object`

Returns animation summary (name, duration, channel count) without full sampler data.

#### `getAllAnimations(): Array<Object>`

Returns all animation clips with full data.

#### `getAllAnimationInfos(): Array<Object>`

Returns all animation summaries.

### Mesh Methods (Skinning-Related Fields)

#### `getMesh(meshId: number): Object`

Mesh data includes skinning fields when the mesh is bound to a skeleton:

```javascript
{
  // ... geometry data (points, normals, texcoords, faceVertexIndices, etc.)
  skel_id: number,              // Skeleton ID (-1 if not skinned)
  jointIndices: Int32Array,     // Joint indices (vertices * influencesPerVertex)
  jointWeights: Float32Array,   // Joint weights (vertices * influencesPerVertex)
  geomBindTransform: Float64Array, // 4x4 matrix (16 doubles)
}
```

### Bone Reduction Settings

#### `setEnableBoneReduction(enable: boolean)`
#### `setTargetBoneCount(count: number)`
#### `setRoundBoneCount(round: number)`

Controls WASM-side bone influence reduction (reduce N influences per vertex to target count). Applied during `loadFromBinary()`.

### Scene Metadata

#### `getUpAxis(): string`

Returns `"Y"` or `"Z"`.

#### `getSceneMetadata(): Object`

Returns scene metadata including `timeCodesPerSecond`, `startTimeCode`, `endTimeCode`.

---

## Pipeline Modules

### USDSkeletonData

Builds skeleton bone data from WASM scene objects.

```javascript
import { buildSkeletonDataFromUSD } from './src/tinyusdz/USDSkeletonData.js';

const skeletonBuild = buildSkeletonDataFromUSD(usdScene, {
  logger,                // Optional logger with .log(), .warn(), .error()
  hasSkinnedMeshData,    // boolean: whether any mesh has skel_id >= 0
  onSkeletonInfo,        // Optional callback({ numSkeletons, totalJointCount })
});
```

**Returns:**
```javascript
{
  numSkeletons: number,
  totalJointCount: number,
  skeletonDataArray: [{       // Per-skeleton data
    skelId: number,
    bones: THREE.Bone[],
    boneMap: Map<jointId, THREE.Bone>,
    rootBone: THREE.Bone,
    boneInverses: THREE.Matrix4[],
    skeletonAbsPath: string,
  }],
  boneMaps: Map<skelId, Map<jointId, THREE.Bone>>,
  firstBones: THREE.Bone[],   // Convenience: skeletonDataArray[0].bones
  firstBoneMap: Map,
  firstRootBone: THREE.Bone,
  firstSkeletonAbsPath: string,
  fallbackSkeletonCreated: boolean,
}
```

### USDSkeletalHelper

Low-level skeleton creation from USD data.

```javascript
import { createThreeSkeletonFromUSD, resetSkeletonToRestPose } from './src/tinyusdz/USDSkeletalHelper.js';
```

#### `createThreeSkeletonFromUSD(usdSkeleton, options?)`

Creates Three.js Bone hierarchy from `usd_scene.getSkeleton(id)`.

- `options.useBindTransforms` (default `true`): Use bind transforms (world-space) to decompose bone local transforms. Falls back to rest transforms if bind are unavailable.
- `options.skelId` (default `0`): Stored in `bone.userData.skel_id`.

**Returns:** `{ bones, boneMap, rootBone, boneInverses }`

#### `resetSkeletonToRestPose(skeleton)`

Resets all bones to bind-derived local transforms (or rest transforms as fallback).

### USDSceneSkinningPipeline

Binds skeletons to skinned meshes and collects all render meshes.

```javascript
import { applyUSDSceneSkinningPipeline } from './src/tinyusdz/USDSceneSkinningPipeline.js';

const skinningResult = applyUSDSceneSkinningPipeline({
  logger,
  threeNode,           // Three.js scene hierarchy from buildThreeNode()
  characterGroup,      // THREE.Group container
  helperScene,         // THREE.Scene for skeleton helpers
  skeletonDataArray,   // From buildSkeletonDataFromUSD()
  allSkinnedMeshUSDData, // Map<meshName, usdMeshData>
  skinnedMeshDataByName, // Map<meshName, { skelId, meshIndex }>
  usdScene,            // WASM scene object
  showMesh,            // boolean: initial mesh visibility
  showSkeleton,        // boolean: initial skeleton helper visibility
  useWASMBoneTexture,  // boolean: use WASM-generated bone texture for extended skinning
});
```

**Returns:**
```javascript
{
  allMeshes: THREE.Mesh[],           // All meshes (skinned + static)
  allSceneMeshes: THREE.Mesh[],      // Static (non-skinned) meshes
  meshVisibility: Map<mesh, boolean>,
  skeletons: Map<skelId, THREE.Skeleton>,
  skeletonHelpers: THREE.SkeletonHelper[],
  firstSkeleton: THREE.Skeleton,
  firstSkeletonHelper: THREE.SkeletonHelper,
  firstSkinnedMesh: THREE.SkinnedMesh,
  primaryMesh: THREE.Mesh,           // Largest mesh for camera framing
  processedSkinnedCount: number,
  hasSkeletonHelpers: boolean,
}
```

**Key behavior:**
- Calls `mesh.bind(skeleton)` without custom args (Three.js calculates world-space inverses)
- Calls `characterGroup.updateMatrixWorld(true)` before binding
- Sets `mesh.frustumCulled = false` on skinned meshes
- Applies extended skinning via `applyExtendedSkinningIfNeeded()`

### USDAnimationConverter

Converts USD animation data to Three.js AnimationClips.

```javascript
import {
  convertUSDSkeletalAnimationsToThreeJS,
  convertUSDNodeAnimationsToThreeJS,
  buildNodeIndexMap,
} from './src/tinyusdz/USDAnimationConverter.js';
```

#### `convertUSDSkeletalAnimationsToThreeJS(usdLoader, boneMaps, timeCodesPerSecond?)`

Converts USD SkeletonJoint channels to Three.js AnimationClips.

- `usdLoader`: WASM scene object
- `boneMaps`: `Map<skelId, Map<jointId, THREE.Bone>>` from skeleton build
- `timeCodesPerSecond`: defaults to 24

**Returns:** `THREE.AnimationClip[]` -- one clip per USD animation that has skeletal channels.

#### `buildNodeIndexMap(threeNode)`

Builds DFS-order index map from the Three.js scene hierarchy. Must be called **before** bones are added to the hierarchy (bone insertion changes DFS order).

**Returns:** `Map<number, THREE.Object3D>`

#### `convertUSDNodeAnimationsToThreeJS(usdLoader, nodeIndexMap)`

Converts USD SceneNode xformOp channels to Three.js AnimationClips. Used for animating skeleton ancestors.

**Returns:** `THREE.AnimationClip[]`

### USDSceneAnimationPipeline

High-level animation extraction and playback controller.

```javascript
import {
  extractUSDSceneAnimations,
  createUSDSceneAnimationPlayback,
  computeUSDSceneTimelineDuration,
} from './src/tinyusdz/USDSceneAnimationPipeline.js';
```

#### `extractUSDSceneAnimations(usdScene, options?)`

Extracts all skeletal and node animation clips from the scene.

**Returns:**
```javascript
{
  animationInfos: Object[],         // Animation summaries
  usdAnimations: THREE.AnimationClip[],      // Skeletal clips
  usdNodeAnimations: THREE.AnimationClip[],  // Node xformOp clips
  animationEnabled: boolean[],       // Per-animation enable flags
  disabledCount: number,
  hasAnyAnimation: boolean,
}
```

#### `createUSDSceneAnimationPlayback(rootObject, options?)`

Creates a playback controller wrapping Three.js AnimationMixer.

**Returns** object with methods:
- `getState()` -- returns `{ mixer, animationAction, animationActions }`
- `playAnimation(index)` -- play single clip, returns state + `{ clip }`
- `playAllAnimations(enabledFlags)` -- play all enabled clips, returns state + `{ enabledCount, skippedCount }`
- `playNodeAnimations(options?)` -- play node animation clips
- `stopAllAnimations()` -- stop and reset all actions
- `setPaused(paused)` -- pause/resume
- `setTime(time, updatePose)` -- seek to time
- `setSpeed(speed)` -- set playback speed
- `reset()` -- stop all and reset mixer
- `dispose()` -- cleanup

#### `computeUSDSceneTimelineDuration(endTimeCode, usdAnimations, usdNodeAnimations)`

Returns max duration across scene metadata and all clips.

### ExtendedSkinning

Handles meshes with more than 4 bone influences per vertex.

```javascript
import {
  SkinningMode,
  getSkinningMode,
  addExtendedSkinningAttributes,
  applyExtendedSkinningIfNeeded,
  createExtendedDepthMaterial,
  createExtendedWeightVisualizationMaterial,
  getGeometrySkinningMode,
} from './src/tinyusdz/ExtendedSkinning.js';
```

#### Skinning Modes

| Mode | Influences/Vertex | Technique |
|------|-------------------|-----------|
| `STANDARD` (4) | 1-4 | Built-in Three.js `skinIndex`/`skinWeight` |
| `EXTENDED_8` (8) | 5-8 | Extra `skinIndex2`/`skinWeight2` attributes |
| `TEXTURE_16`+ | 9-128 | Bone data texture lookup in vertex shader |

#### `applyExtendedSkinningIfNeeded(skinnedMesh, options?)`

Automatically detects skinning mode from geometry attributes and applies shader modifications via `onBeforeCompile`. Also creates matching `customDepthMaterial` for correct shadow rendering.

Sets `mesh._useTexSkinUniform` for runtime toggle between texture skinning and 4-bone fallback.

**Returns:** `boolean` -- true if extended skinning was applied.

#### `createExtendedWeightVisualizationMaterial(options?)`

Creates a ShaderMaterial that visualizes bone weights as pseudo-colors. Supports all skinning modes.

---

## Quick Start Example

```javascript
import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import { buildSkeletonDataFromUSD } from './src/tinyusdz/USDSkeletonData.js';
import { applyUSDSceneSkinningPipeline } from './src/tinyusdz/USDSceneSkinningPipeline.js';
import {
  extractUSDSceneAnimations,
  createUSDSceneAnimationPlayback,
} from './src/tinyusdz/USDSceneAnimationPipeline.js';

// 1. Load USD file via WASM
const loader = new TinyUSDZLoader();
await loader.init();
const usdScene = await loader.loadFromBinary(arrayBuffer);

// 2. Build Three.js scene hierarchy
const { threeNode } = loader.buildThreeNode(usdScene);
const characterGroup = new THREE.Group();
characterGroup.add(threeNode);
scene.add(characterGroup);

// 3. Build skeleton data
const skeletonBuild = buildSkeletonDataFromUSD(usdScene);

// 4. Bind skeletons to skinned meshes
characterGroup.updateMatrixWorld(true);
const skinningResult = applyUSDSceneSkinningPipeline({
  threeNode, characterGroup, helperScene: scene,
  skeletonDataArray: skeletonBuild.skeletonDataArray,
  allSkinnedMeshUSDData, skinnedMeshDataByName, usdScene,
  showMesh: true, showSkeleton: false,
});

// 5. Extract and play animations
const animResult = extractUSDSceneAnimations(usdScene, {
  boneMaps: skeletonBuild.boneMaps,
});
const playback = createUSDSceneAnimationPlayback(characterGroup, {
  usdAnimations: animResult.usdAnimations,
  usdNodeAnimations: animResult.usdNodeAnimations,
});
playback.playAllAnimations(animResult.animationEnabled);

// 6. Copy WASM data, then free
usdScene.delete();

// 7. Render loop
const clock = new THREE.Clock();
function animate() {
  const state = playback.getState();
  state.mixer?.update(clock.getDelta());
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}
animate();
```

---

## Data Structures

### Transform Matrices

USD provides two transform types per joint:

- **Bind Transform**: Joint-to-world-space transform at bind time (used for skinning math)
- **Rest Transform**: Joint's neutral/rest local pose

Matrices are **column-major** 4x4 (16 doubles), matching Three.js `Matrix4.elements` order:
```
[m00, m10, m20, m30, m01, m11, m21, m31, m02, m12, m22, m32, m03, m13, m23, m33]
```

### Animation Keyframe Values

Per-channel value layout in `sampler.values`:

| Path | Values per keyframe | Layout |
|------|-------------------|--------|
| Translation | 3 floats | `[x, y, z]` |
| Rotation | 4 floats | `[x, y, z, w]` (quaternion) |
| Scale | 3 floats | `[sx, sy, sz]` |

### Skinning Attributes

For a mesh with `N` vertices and `K` influences per vertex:

- `jointIndices`: `Int32Array` of length `N * K` -- bone indices
- `jointWeights`: `Float32Array` of length `N * K` -- weights (sum to 1.0 per vertex)

Example (vertex 0, 4 influences):
```javascript
jointIndices[0..3] = [2, 5, 7, 0]     // Influenced by joints 2, 5, 7, 0
jointWeights[0..3] = [0.5, 0.3, 0.15, 0.05]  // Sum = 1.0
```

---

## Troubleshooting

### Mesh doesn't deform
- Verify `skel:skeleton` relationship and `primvars:skel:jointIndices`/`jointWeights` in the USD file
- Check that `characterGroup.updateMatrixWorld(true)` is called before `mesh.bind(skeleton)`

### Wrong scale or rotation at bind pose
- **Never pass USD boneInverses to `bind()`** -- they're in USD skeleton-local space, not Three.js world space
- **Never pass geomBindTransform as bindMatrix** -- space mismatch with `calculateInverses()`
- Use `mesh.bind(skeleton)` with no extra arguments

### Stretched shadows on animated poses
- Extended skinning `customDepthMaterial` must match the render material's shader modifications
- Check console for "Extended skinning mode" log to confirm it was applied

### Z-up model appears sideways
- Toggle "Z-up -> Y-up" in the Visualization panel
- The viewer auto-detects `upAxis` from scene metadata

### Animation data appears as zeros
- WASM `typed_memory_view` arrays must be copied before `usd_scene.delete()` -- see `docs/WASM_TYPED_MEMORY_VIEW.md`

---

## See Also

- **[SKELETAL_ANIMATION.md](../SKELETAL_ANIMATION.md)** -- Architecture overview and viewer features
- **[WASM_TYPED_MEMORY_VIEW.md](WASM_TYPED_MEMORY_VIEW.md)** -- WASM memory safety
- **[doc/skin-eval.md](../../../doc/skin-eval.md)** -- Skinning equation derivation (USD vs Three.js)
- **[doc/skinning.md](../../../doc/skinning.md)** -- USD skinning spec reference
- [THREE.Skeleton](https://threejs.org/docs/#api/en/objects/Skeleton) | [THREE.SkinnedMesh](https://threejs.org/docs/#api/en/objects/SkinnedMesh) | [THREE.AnimationMixer](https://threejs.org/docs/#api/en/animation/AnimationMixer)
