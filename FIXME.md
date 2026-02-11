# FIXME: Reverted Commit 9af8937b5

## Status
Commit `9af8937b5` ("Fix extended skinning shadow stretching and AttachedBindMode alignment")
was reverted. Branch `skinning-phase1-fix` is based on its parent `905ebb501` with only the
`src/timesamples.hh` one-line fix cherry-picked.

## What the Commit Tried to Do

### 1. customDepthMaterial for Extended Skinning Shadows (ExtendedSkinning.js) — APPLIED
- Meshes with >4 bone influences use `onBeforeCompile` to patch the vertex shader
  (8-bone: `skinIndex2`/`skinWeight2` attributes; 16+: bone data texture).
- Three.js auto-generated shadow depth material only supports 4 bones per vertex.
- **Fix**: Added `createExtendedDepthMaterial()` that creates a `MeshDepthMaterial` with the
  same shader modifications, assigned as `mesh.customDepthMaterial`.
- Without this: bones 5-8 ignored in shadow pass → vertices stuck at bind pose → stretched shadows.
- **Re-applied in commit after 64ffbffe6.**

### 2. AttachedBindMode (skin-anim.js) — FIXED (64ffbffe6)
- Removed explicit `DetachedBindMode` usage.
- AttachedBindMode (Three.js default): `bindMatrixInverse = inverse(mesh.matrixWorld)` each frame.
  This correctly handles ancestor transform changes (Z-up toggle, etc.).
- DetachedBindMode uses stale `bindMatrixInverse` from bind time, which breaks when
  `characterGroup.rotation` changes.
- **Fixed in 64ffbffe6: hierarchy-preserving approach uses AttachedBindMode (default).**

### 3. Preserve threeNode Hierarchy / In-Place Mesh Replacement (skin-anim.js) — FIXED (64ffbffe6)
The largest and most impactful change. Instead of extracting meshes from `threeNode` and
re-parenting them directly to `characterGroup`, the commit:
- Adds `threeNode` (USD scene graph) to `characterGroup` directly.
- Replaces each Mesh with a SkinnedMesh in-place within the hierarchy.
- Calls `characterGroup.updateMatrixWorld(true)` then `mesh.bind(skeleton)` with no args,
  letting Three.js compute `bindMatrix = mesh.matrixWorld` and `boneInverses` from
  `bone.matrixWorld`.

**What went wrong**: The dragon model (AnimFinal_LowRes.usdz) has:
- `upAxis="Z"` (file is Z-up)
- `geomBindTransform` containing a +90°X rotation matrix per mesh
  (transforms Y-up mesh-local vertices to Z-up skeleton space)
- `buildThreeNode()` (Tydra C++) applies `geomBindTransform` as a parent group
  with `rotation.x = +90°` for each mesh

With the old approach (parent commit `905ebb501`), meshes were extracted from the hierarchy
and geomBindTransform was passed to `mesh.bind(skeleton, geomBindTransform)`, letting
Three.js use it as the bind matrix directly. This worked.

With the new approach, the geomBindTransform is baked into the hierarchy as a parent group
transform. When `mesh.bind(skeleton)` is called, `mesh.matrixWorld` includes the
geomBindTransform (+90°X), which gets captured as `bindMatrix`. The `characterGroup.rotation.x
= -Math.PI/2` (Z-up to Y-up) then adds another -90°X. The interaction between:
- geomBindTransform parent group (+90°X)
- characterGroup rotation (-90°X for Z-up → Y-up)
- bindMatrix (captured at bind time, includes geomBindTransform)
- boneInverses (recalculated from bone.matrixWorld)

...did not produce the correct final transform. The dragon appeared flat/squished.

**Root cause**: The original attempt called `mesh.bind(skeleton)` with no args, which
triggered `calculateInverses()` and overwrote USD boneInverses. **Fix** (64ffbffe6): pass
`geomBindTransform` explicitly to `bind()`, preventing `calculateInverses()`. This correctly
handles all three test models (AnimFinal, CesiumMan, StandingRunForward).

### 4. Z-up Conversion Simplification (skin-anim.js) — FIXED (64ffbffe6)
- Replaced the per-mesh approach (rotate static geometry, prepend R to `bindMatrixInverse`
  for skinned meshes) with simple `characterGroup.rotation.x = -Math.PI / 2`.
- This is cleaner but only works if the hierarchy and binding are set up correctly (see #3).
- Removed `detectedZUpFromPath` override (checked mesh paths for "Z_UP" string).

### 5. characterGroup Transform Reset (skin-anim.js) — APPLIED (64ffbffe6)
- Reset `characterGroup.rotation/position/scale` at start of `processUSDScene()`.
- Prevents stale transforms from a previous file load leaking into `bind()`.
- **Applied in 64ffbffe6.**

### 6. Resource Disposal (skin-anim.js) — APPLIED
- Added proper disposal of geometries, materials, textures, joint spheres, skeleton
  bone textures, and skeleton helper on file reload.
- Previously these were leaking GPU resources.
- **Re-applied in commit after 64ffbffe6.**

### 7. GC Pressure / Allocation Optimizations (skin-anim.js) — APPLIED
- Added module-level reusable temporaries (`_tmpVec3`, `_tmpBox3`, etc.).
- Replaced per-frame `new THREE.Vector3()` allocations with reusable temporaries in:
  `updateJointSpheres`, `computeSceneBoundingBox`, `updateShadowCameraFromBounds`,
  `computeSkinnedBBox`, `expandBoxByMeshBones`, `expandBoxBySkeletonBones`,
  `computeCurrentBBox`, `fitCameraToScene`.
- Cached per-mesh bone indices in `_meshBoneIndexCache` WeakMap.
- Removed `Array.from()` copies for keyframe track times/values (Three.js accepts TypedArrays).
- Adaptive sample count in `fitCameraToScene` for large skeletons.
- **Re-applied in commit after 64ffbffe6.**

### 8. Misc UI/Playback Fixes (skin-anim.js) — APPLIED
- Used `animationAction.setEffectiveTimeScale()` for speed instead of scaling `mixer.update()` delta.
- Added `mixer.update(0)` on timeline scrub when paused (propagates pose without advancing).
- Changed default `showSkeleton` to `false`.
- Added `.listen()` to Z-up toggle checkbox.
- Skip `skeletonHelper.update()` when not visible.
- **Re-applied in commit after 64ffbffe6.**

### 9. timesamples.hh Fix (src/timesamples.hh) — ALREADY APPLIED
- Line 1209: `return true;` → `return false;` when `as<T>()` fails for single-sample data.
- **Already cherry-picked to skinning-phase1-fix branch.**

## Plan for Re-application

Re-apply changes in stages, testing with BOTH AnimFinal_LowRes.usdz (Z-up + geomBindTransform)
and CesiumMan.usdz (Y-up + Z_UP xform node) at each stage:

1. **Stage A**: GC optimizations, resource disposal, characterGroup reset, misc UI fixes
   (items 5, 6, 7, 8) — safe, no rendering logic changes.

2. **Stage B**: customDepthMaterial for extended skinning shadows (item 1) — safe, only
   adds shadow material.

3. **Stage C**: Hierarchy preservation + AttachedBindMode + Z-up conversion (items 2, 3, 4) —
   **this is the problematic part**. Needs careful rework:
   - Must handle the case where `geomBindTransform` is a rotation in the hierarchy AND the
     file is Z-up.
   - Options to investigate:
     a) Strip geomBindTransform from the hierarchy and pass it to `bind()` explicitly.
     b) Apply geomBindTransform to vertex data before binding (pre-transform geometry).
     c) Compute a corrected bindMatrix that accounts for the geomBindTransform in the hierarchy.
     d) Use the old per-mesh approach for Z-up conversion (geometry rotation for static,
        bindMatrixInverse adjustment for skinned) instead of characterGroup rotation.

## USD Skinning Formula (Blender Reference)

Reference: `blender-git/source/blender/io/usd/intern/usd_skel_convert.cc`

### USD Skeleton Attributes

| Attribute | Space | Description |
|-----------|-------|-------------|
| `bindTransforms` | skeleton world | Joint transforms at bind time (world space within skeleton) |
| `restTransforms` | joint local | Joint transforms at rest pose (local to parent joint) |
| `geomBindTransform` | mesh → skeleton | Transforms mesh-local vertices into skeleton space |
| `SkelAnimation` | joint local | Time-sampled joint-local transforms |

### Blender's Import Approach

#### 1. Bone rest pose ← bindTransforms (world space)

Blender uses world-space `bindTransforms` directly as edit bone matrices:

```
EditBone.matrix = bindTransforms[i]   // world space, used as-is
```

#### 2. Joint-local bind transforms (derived from world bindTransforms)

```
if joint has parent:
    jointLocalBind[i] = bindTransforms[i] * inv(bindTransforms[parent])
else:
    jointLocalBind[i] = bindTransforms[i]   // root joint: world = local
```

#### 3. Rest pose (delta from bind)

```
restLocal[i] = ComputeJointLocalTransforms(defaultTime, atRest=true)

// Same local-from-world derivation for bind:
localBind[i] = bindTransforms[i] * inv(bindTransforms[parent])

// Pose channel transform = rest relative to bind
poseTransform[i] = restLocal[i] * inv(localBind[i])
```

#### 4. Animation curves (per-frame)

```
jointLocalXform[i] = ComputeJointLocalTransforms(frame)

// Animation = current local transform relative to local bind transform
boneAnimXform[i] = jointLocalXform[i] * inv(jointLocalBind[i])

// Decompose into (translation, quaternion, scale) for fcurves
```

This is the transform that Blender applies as pose bone loc/rot/scale.

#### 5. geomBindTransform — NOT read on import

Blender **does not read `geomBindTransform` during import**. Instead, it relies on the
Blender scene hierarchy (mesh object transform relative to armature object) to implicitly
provide this transform. The armature modifier in Blender handles the mesh↔skeleton
spatial relationship through the object hierarchy.

On **export**, Blender computes it as:
```
geomBindTransform = meshWorldXform * inv(skeletonWorldXform)
```

#### 6. Blender's runtime skinning formula (armature modifier)

```
skinnedPos = sum(w_i * boneDeformMatrix_i * vertexPos)

boneDeformMatrix_i = poseBoneWorld_i * inv(restBoneWorld_i)
```

Where:
- `restBoneWorld_i` = `bindTransforms[i]` (set during import)
- `poseBoneWorld_i` = animated bone world matrix (rest + animation delta accumulated through hierarchy)
- At bind/rest pose: `boneDeformMatrix = identity`, so `skinnedPos = vertexPos`

### Mapping to Three.js

```
// Three.js skinning shader:
skinnedPos = bindMatrixInverse * sum(w_i * boneMatrix_i) * bindMatrix * vertexPos

boneMatrix_i = bone.matrixWorld * skeleton.boneInverses[i]
```

| Blender | Three.js |
|---------|----------|
| `inv(restBoneWorld_i)` | `skeleton.boneInverses[i]` |
| `poseBoneWorld_i` | `bone.matrixWorld` |
| implicit (object hierarchy) | `bindMatrix` = `mesh.matrixWorld` at bind time |
| implicit (object hierarchy) | `bindMatrixInverse` = `inv(mesh.matrixWorld)` |
| — | At bind pose: `boneMatrix_i = identity` → `skinnedPos = vertexPos` |

### Mapping to TinyUSDZ Viewer (skin-anim.js)

#### What Tydra C++ provides:
- `bindTransforms` → world-space joint matrices (same as Blender reads)
- `restTransforms` → joint-local rest matrices
- `geomBindTransform` → mesh-to-skeleton transform (baked into threeNode hierarchy by `buildThreeNode`)
- `SkelAnimation` → joint-local time-sampled transforms (converted to keyframe tracks)

#### What the viewer must do:

1. **Build bones** from `bindTransforms` or `restTransforms` (currently uses `bindTransforms`
   to derive local transforms via parent chain)

2. **Create skeleton**: `skeleton.boneInverses[i]` should equal `inv(bindTransforms[i])`
   in skeleton space. Currently computed by `skeleton.calculateInverses()` from
   `bone.matrixWorld` after hierarchy setup.

3. **Handle geomBindTransform**: This is where TinyUSDZ differs from Blender.
   Blender ignores it and lets the object hierarchy handle mesh↔skeleton positioning.
   In Three.js, the options are:
   - Pass to `mesh.bind(skeleton, geomBindTransform)` as the bind matrix
   - Let Three.js compute it from `mesh.matrixWorld` (if hierarchy is correctly set up)
   - The current code passes it to `bind()` explicitly — this works

4. **Animation**: Tydra converts `SkelAnimation` joint-local transforms into
   Three.js keyframe tracks (position, quaternion, scale). These are relative to
   the parent bone, matching Three.js bone.position/quaternion/scale semantics.

### Key Insight: geomBindTransform Handling

Blender's approach (ignore geomBindTransform, rely on object hierarchy) works because
Blender's armature modifier implicitly resolves mesh-to-skeleton positioning through
the scene graph. Three.js requires explicit `bindMatrix`/`bindMatrixInverse` to achieve
the same effect.

The current working approach in skin-anim.js (passing geomBindTransform to `bind()`)
is correct. The failed approach (baking geomBindTransform into the scene hierarchy AND
using `mesh.matrixWorld` as bindMatrix) double-counted the transform.

## Files Changed in 9af8937b5

| File | Lines | Status |
|------|-------|--------|
| `src/timesamples.hh` | +1/-1 | Already applied |
| `web/js/skin-anim.js` | +272/-220 | Reverted (needs staged re-apply) |
| `web/js/src/tinyusdz/ExtendedSkinning.js` | +49/-6 | Reverted (safe to re-apply) |
| `web/js/src/tinyusdz/TinyUSDZLoaderUtils.js` | +0/-1 | Reverted (trivial, safe to re-apply) |
