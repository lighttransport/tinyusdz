# Skeletal Animation

USD skeletal animation extraction and playback using Three.js SkinnedMesh.

## Entry Points

- **skin-anim.html** → `skin-anim.js` — Full skeletal animation viewer with GUI
- **anim-clips.html** → `anim-clips.js` — Per-object animation clip mixing
- **animation.html** → `animation.js` — Basic animation playback

## Pipeline

```
USD File → TinyUSDZLoader (WASM parse) → usd_scene
  → TinyUSDZLoaderUtils.buildThreeNode() → Three.js scene hierarchy
  → USDSkeletonData.buildSkeletonDataFromUSD() → bone maps
  → USDSceneSkinningPipeline.applyUSDSceneSkinningPipeline() → SkinnedMesh binding
  → USDAnimationConverter → AnimationClips
  → USDSceneAnimationPipeline.createUSDSceneAnimationPlayback() → playback controller
  → ExtendedSkinning.applyExtendedSkinningIfNeeded() → >4 bone support
```

## Pipeline Modules (`src/tinyusdz/`)

| Module | Purpose |
|--------|---------|
| `USDSkeletonData.js` | Build skeleton bone maps from WASM scene data |
| `USDSceneSkinningPipeline.js` | Bind skeletons to skinned meshes, collect render meshes |
| `USDAnimationConverter.js` | Convert USD channels/samplers to Three.js AnimationClips |
| `USDSceneAnimationPipeline.js` | Extract animations + create playback controller |
| `USDSkeletalHelper.js` | Create Three.js Skeleton from USD, reset to rest pose |
| `ExtendedSkinning.js` | Extended skinning for >4 bones per vertex |

## Key Design Decisions

**Preserve full scene graph**: TinyUSDZ preserves all xformOps on all nodes including skinned meshes. Three.js world-space `bind()` handles the transform math correctly (see `doc/skinning.md` for the full derivation).

**bind() without custom args**: `mesh.bind(skeleton)` is called without passing boneInverses or bindMatrix. Three.js internally computes `boneInverses = inv(bone.matrixWorld)` and `bindMatrix = mesh.matrixWorld`, both in world space. Passing USD-space boneInverses would mix coordinate spaces.

**AttachedBindMode**: Skinned meshes use AttachedBindMode so `bindMatrixInverse` is recomputed from `mesh.matrixWorld` each frame. This handles post-bind hierarchy changes (e.g., Z-up toggle).

## Skinning Equation

USD (column-vector convention):
```
skinnedPoint = Σ(w_i * inv(bindTransforms[i]) * jointSkelTransform[i] * geomBindTransform * localPoint)
```

Three.js with AttachedBindMode:
```
world_pos = Σ(w_i * bone.matrixWorld * boneInverse_i * bindMatrix * pos)
```

These are equivalent. With AttachedBindMode, `inv(mesh.matrixWorld)` replaces `bindMatrixInverse` each frame, so mesh xformOps cancel out — consistent with the USD spec rule that xformOps on skinned prims are ignored.

### USD → Three.js Mapping

| USD Concept | Three.js Equivalent |
|-------------|---------------------|
| `geomBindTransform` | `mesh.bindMatrix` |
| `inv(Skeleton.bindTransforms[i])` | `skeleton.boneInverses[i]` |
| `jointSkelTransform * skelLocalToWorld` | `bone.matrixWorld` |
| (cancelled by AttachedBindMode) | `bindMatrixInverse = inv(mesh.matrixWorld)` |

## Extended Skinning

| Mode | Influences/Vertex | Technique |
|------|-------------------|-----------|
| `STANDARD` (4) | 1-4 | Built-in Three.js `skinIndex`/`skinWeight` |
| `EXTENDED_8` (8) | 5-8 | Extra `skinIndex2`/`skinWeight2` attributes |
| `TEXTURE_16`+ | 9-128 | Bone data texture lookup in vertex shader |

Auto-detected from geometry. Includes matching `customDepthMaterial` for correct shadow rendering.

## WASM Binding API

### Skeleton

```javascript
numSkeletons(): number
getSkeleton(skelId): { id, prim_name, abs_path, anim_id, anim_ids, root_node: { joint_path, joint_name, joint_id, bind_transform: Float64Array, rest_transform: Float64Array, children } }
getSkeletonJointsFlat(skelId): { num_joints, joint_names, joint_paths, parent_indices: Int32Array, bind_matrices: Float64Array, rest_matrices: Float64Array }
```

### Animation

```javascript
getAnimation(animId): { id, name, duration, channels: [{ sampler, skeleton_id, joint_id, target_type, path }], samplers: [{ times: Float32Array, values: Float32Array, interpolation }] }
```

Channel `target_type`: `"SkeletonJoint"` (skeletal) or `"SceneNode"` (xformOp).
Channel `path`: `"Translation"` (3 floats), `"Rotation"` (4 floats, quaternion XYZW), `"Scale"` (3 floats).

### Mesh Skinning Fields

```javascript
getMesh(meshId): { ..., skel_id, jointIndices: Int32Array, jointWeights: Float32Array, geomBindTransform: Float64Array }
```

Matrices are column-major 4x4 (16 doubles), matching Three.js `Matrix4.elements` order.

**IMPORTANT**: All typed arrays from WASM are views into heap memory. Copy them before calling `usd_scene.delete()` — see `docs/WASM_TYPED_MEMORY_VIEW.md`.

## CLI Tool: skinning-info.js

```bash
npx vite-node skinning-info.js <usd-file> [options]
  --detailed        Print detailed skinning and animation data
  --keyframes       Dump skeletal animation keyframe data
  --memory          Print memory usage statistics
  --reduce-bones    Enable bone reduction during loading
  --round-bones     Round bone count to standard values (4,8,16,32,48,64,80,96,128)
  --target-bones N  Target bone count per vertex (default: 4)
  --bone-texture    Test bone texture generation for GPU skinning
  --transforms      Dump node tree xforms and skeleton bind/rest transforms
```

## Z-up to Y-up Conversion

With AttachedBindMode, rotating `characterGroup` (ancestor of both mesh and bones) applies rotation R to both `bone.matrixWorld` and `mesh.matrixWorld`. Since `inv(R*M) * R = inv(M)`, the R cancels in the skinning equation and only affects the final world-space result — correctly rotating the entire skinned output.

```javascript
characterGroup.rotation.x = -Math.PI / 2;  // Z-up → Y-up
characterGroup.rotation.x = 0;             // Reset
```

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| Mesh doesn't deform | Missing `skel:skeleton`, `jointIndices`, or `jointWeights` | Check USD file |
| Wrong scale/rotation at bind | Custom args passed to `bind()` | Use `mesh.bind(skeleton)` with no extra args |
| Stretched shadows | Missing `customDepthMaterial` for extended skinning | Check console for skinning mode log |
| Z-up model sideways | Missing up-axis conversion | Toggle "Z-up → Y-up" in GUI |
| Animation data zeros | WASM arrays read after `usd_scene.delete()` | Copy arrays before delete |

## Related

- `doc/skinning.md` — USD skinning spec and equation derivations
- `docs/WASM_TYPED_MEMORY_VIEW.md` — WASM memory safety
