# Skeletal Animation Viewer

USD skeletal animation extraction and playback using Three.js SkinnedMesh. Supports multiple skeletons, extended skinning (4/8/16/32/64+ bones per vertex), node animations, and interactive joint manipulation.

## Files

### Entry Point
- **skin-anim.html** - HTML page
- **skin-anim.js** - Main viewer: scene setup, GUI, render loop, file loading

### Pipeline Modules (`src/tinyusdz/`)
- **USDSkeletonData.js** - Build skeleton bone maps from WASM scene data
- **USDSceneSkinningPipeline.js** - Bind skeletons to skinned meshes, collect render meshes
- **USDAnimationConverter.js** - Convert USD channels/samplers to Three.js AnimationClips
- **USDSceneAnimationPipeline.js** - Extract animations + create playback controller
- **USDSkeletalHelper.js** - Create Three.js Skeleton from USD, reset to rest pose
- **ExtendedSkinning.js** - Extended skinning for >4 bones (attribute-based 8, texture-based 16+)
- **TinyUSDZLoader.js** - WASM module loading and USD file parsing
- **TinyUSDZLoaderUtils.js** - Build Three.js scene graph from USD render data

## Architecture

### Pipeline Overview

```
USD File
  |
  v
TinyUSDZLoader (WASM parse) --> usd_scene object
  |
  v
TinyUSDZLoaderUtils.buildThreeNode() --> Three.js scene hierarchy (threeNode)
  |
  +---> USDSkeletonData.buildSkeletonDataFromUSD()
  |       --> skeleton bone maps, bone arrays
  |
  +---> USDSceneSkinningPipeline.applyUSDSceneSkinningPipeline()
  |       --> SkinnedMesh binding, skeleton helpers
  |
  +---> USDAnimationConverter (skeletal + node clips)
  |     USDSceneAnimationPipeline.extractUSDSceneAnimations()
  |       --> AnimationClips[]
  |
  +---> USDSceneAnimationPipeline.createUSDSceneAnimationPlayback()
  |       --> playback controller (mixer, actions)
  |
  +---> ExtendedSkinning.applyExtendedSkinningIfNeeded()
          --> shader modifications for >4 bones
```

### Key Design Decisions

**Preserve full scene graph (NOT literal USD spec):**
TinyUSDZ preserves all xformOps on all nodes including skinned meshes, rather than following the USD spec which ignores mesh transforms for skinned prims. Three.js world-space `bind()` handles the transform math correctly. See `doc/skin-eval.md` for the full derivation.

**bind() without custom args:**
`mesh.bind(skeleton)` is called without passing boneInverses or bindMatrix. Three.js internally computes `boneInverses = inv(bone.matrixWorld)` and `bindMatrix = mesh.matrixWorld`, both in world space. Passing USD-space boneInverses would mix coordinate spaces and produce incorrect results.

**AttachedBindMode (default):**
Skinned meshes use AttachedBindMode so that `bindMatrixInverse` is recomputed from `mesh.matrixWorld` each frame. This correctly handles post-bind hierarchy changes (e.g., Z-up toggle).

**Multi-skeleton support:**
The viewer supports scenes with multiple skeletons (e.g., Layout_Scene.usdz with 6 skeletons). Skeletons are stored in a `Map<skelId, THREE.Skeleton>`, and bone maps are per-skeleton.

## Features

### Skinning
- Multiple skeletons per scene
- Extended skinning: 4 (standard), 8 (attribute-based), 16/32/48/64/80/96/128 (texture-based) bones per vertex
- Runtime toggle between extended and 4-bone fallback skinning
- Correct shadow rendering for extended skinning (customDepthMaterial)
- Point-based deformation rigs (3000+ joints)

### Animation
- Skeletal joint animations (Translation, Rotation, Scale)
- Node/ancestor xformOp animations
- Play individual or all animations simultaneously
- Per-animation enable/disable checkboxes
- Timeline scrubbing with looping
- Speed control

### Visualization
- Skeleton helper overlay (toggle per skeleton)
- Joint spheres with raycasting selection
- Per-mesh visibility toggles
- Weight visualization (blended colors, intensity, influence count)
- Bounding box display (per-mesh and scene-wide)
- CPU skinning debug mode
- Raw mesh (unskinned) display

### Interaction
- Joint selection via click (raycasts skinned mesh geometry)
- TransformControls for selected joints (translate/rotate/scale, world/local space)
- Joint hierarchy tree in GUI

## Usage

```bash
cd web/js
bun install   # or npm install
bun run dev   # starts vite dev server
# Open http://localhost:5173/skin-anim.html
```

1. Click "Load USD File" or drag-and-drop a `.usdz`/`.usdc`/`.usda` file
2. Skeleton and animations are extracted and played automatically
3. Use the GUI panel on the right to control playback, visualization, and joint manipulation

## Module API Reference

### USDSkeletonData

```javascript
import { buildSkeletonDataFromUSD } from './src/tinyusdz/USDSkeletonData.js';

const skeletonBuild = buildSkeletonDataFromUSD(usdScene, {
  logger, hasSkinnedMeshData, onSkeletonInfo
});
// Returns: { numSkeletons, totalJointCount, skeletonDataArray, boneMaps,
//            firstBones, firstBoneMap, firstRootBone, firstSkeletonAbsPath }
```

### USDSkeletalHelper

```javascript
import { createThreeSkeletonFromUSD, resetSkeletonToRestPose } from './src/tinyusdz/USDSkeletalHelper.js';

const { bones, boneMap, rootBone, boneInverses } = createThreeSkeletonFromUSD(usdSkeleton, {
  useBindTransforms: true, skelId: 0
});

resetSkeletonToRestPose(skeleton);
```

### USDSceneSkinningPipeline

```javascript
import { applyUSDSceneSkinningPipeline } from './src/tinyusdz/USDSceneSkinningPipeline.js';

const skinningResult = applyUSDSceneSkinningPipeline({
  logger, threeNode, characterGroup, helperScene,
  skeletonDataArray, allSkinnedMeshUSDData, skinnedMeshDataByName,
  usdScene, showMesh, showSkeleton, useWASMBoneTexture
});
// Returns: { allMeshes, skeletons (Map), skeletonHelpers, firstSkeleton,
//            primaryMesh, processedSkinnedCount }
```

### USDSceneAnimationPipeline

```javascript
import {
  extractUSDSceneAnimations,
  createUSDSceneAnimationPlayback,
  computeUSDSceneTimelineDuration
} from './src/tinyusdz/USDSceneAnimationPipeline.js';

const animResult = extractUSDSceneAnimations(usdScene, {
  logger, boneMaps, nodeIndexMap, timeCodesPerSecond
});
// Returns: { usdAnimations, usdNodeAnimations, animationEnabled, hasAnyAnimation }

const playback = createUSDSceneAnimationPlayback(rootObject, {
  logger, usdAnimations, usdNodeAnimations, speed
});
// Methods: playAnimation(i), playAllAnimations(enabled), stopAllAnimations(),
//          setTime(t, updatePose), setPaused(p), setSpeed(s), reset(), dispose()
```

### ExtendedSkinning

```javascript
import {
  SkinningMode, getSkinningMode,
  addExtendedSkinningAttributes,
  applyExtendedSkinningIfNeeded,
  createExtendedDepthMaterial
} from './src/tinyusdz/ExtendedSkinning.js';

// Automatically applied during skinning pipeline:
applyExtendedSkinningIfNeeded(skinnedMesh, { useWASMBoneTexture });
// Returns true if extended skinning was applied
```

## USD Channel Structure

Skeletal animation channels from WASM:

```javascript
{
  target_type: 'SkeletonJoint',
  skeleton_id: 0,          // Index into skeletons array
  joint_id: 5,             // Index into skeleton's joints array
  path: 'Rotation',        // Translation / Rotation / Scale
  sampler: 2               // Index into samplers array
}
```

Node animation channels (for ancestor xformOps):

```javascript
{
  target_type: 'SceneNode',
  target_node: 3,          // DFS index into scene hierarchy
  path: 'Translation',
  sampler: 0
}
```

## Troubleshooting

**Mesh doesn't deform**: Check that `skel:skeleton` relationship is authored on the mesh and that `primvars:skel:jointIndices` / `primvars:skel:jointWeights` exist.

**Stretched shadows at animated poses**: Extended skinning customDepthMaterial may not have been applied. Check console for skinning mode info.

**Wrong scale/rotation at bind**: Ensure `characterGroup.updateMatrixWorld(true)` is called before `mesh.bind(skeleton)`. The bind call must see correct world-space bone positions.

**Z-up model appears sideways**: The viewer auto-detects upAxis and applies a rotation. Toggle "Z-up -> Y-up" in the Visualization panel.
