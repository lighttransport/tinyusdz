# Skinning in UsdSkel

Reference: https://openusd.org/dev/api/_usd_skel__intro.html

## Core Components

* **SkelRoot** : Encapsulation prim. Signals that a subtree contains skinned prims. Any `SkelBindingAPI` bindings without a `UsdSkelRoot` ancestor are invalid.
* **Skeleton** : Encodes joint hierarchy via `joints` (ordered token array of `SdfPath` strings). Stores `bindTransforms` (world-space) and `restTransforms` (joint-local-space).
* **SkelAnimation** : Stores per-joint animation as TRS components in joint-local space. Transform construction order: Scale-Rotate-Translate.
* **UsdSkelBindingAPI** : Connects meshes to skeletons. Defines `skel:skeleton`, `skel:animationSource`, `primvars:skel:jointIndices/jointWeights`, `primvars:skel:geomBindTransform`.

## Terminology

* `bindTransforms` : **World-space** matrices of each joint at bind time (from `Skeleton.bindTransforms`)
* `restTransforms` : **Joint-local-space** matrices of each joint at rest pose (from `Skeleton.restTransforms`). Used as fallback when `SkelAnimation` does not supply a transform for a joint.
* `geomBindTransform` : **World-space** transform of the skinned mesh primitive at bind time (from `primvars:skel:geomBindTransform`). Defaults to identity if not authored.
* `SkelAnimation` : Per-joint animation in joint-local space (TRS components)
* Sparse : When `SkelAnimation` maps animation to a subset of joints (others fall back to `restTransforms`)
* Non-Sparse : When `SkelAnimation` maps animation to all joints

Both `bindTransforms` and `restTransforms` must exist in USD for correct skinning. Neither is automatically computed from the other by most implementations (usdview, Houdini, TinyUSDZ).

## Transform Spaces

| Space | Description |
|-------|-------------|
| **Joint Local** | Individual joint transforms (animation TRS). Used for blending. |
| **Skeleton Space** | "Object space" of the Skeleton. Output space of the skinning equation. Does NOT include the Skeleton prim's world positioning. |
| **World Space** | Global scene space. `bindTransforms` and `geomBindTransform` are both in this space. |

## Transform Computations

```
# Joint local → skeleton space (concatenate parent chain)
jointSkelTransform = jointLocalTransform * parentJointSkelTransform
  (identity for root joints)

# Skeleton space → world space
jointWorldTransform = jointSkelTransform * skelLocalToWorldTransform

# Animation TRS → local transform
jointLocalTransform = MakeTransform(scale, rotation, translation)
  (order: Scale-Rotate-Translate)
```

## Skinning Transform

The per-joint skinning transform describes the change from bind pose to animated pose:

```
skinningTransform[i] = inv(bindTransforms[i]) * jointSkelTransform[i]
```

Where:
- `bindTransforms[i]` = world-space bind pose of joint `i` (from `Skeleton.bindTransforms`)
- `jointSkelTransform[i]` = current animated skeleton-space transform of joint `i`

This is what `UsdSkelSkeletonQuery::ComputeSkinningTransforms()` computes.

**Key insight**: `bindTransforms` are in **world space** while animated transforms are in **skeleton space**. The inverse-bind operation correctly bridges these spaces.

## Skinning a Point (Linear Blend Skinning)

```
# Step 1: Transform mesh-local point into bind space (world-aligned at bind time)
skelSpacePoint = geomBindTransform.Transform(localSpacePoint)

# Step 2: Apply weighted skinning transforms
skinnedPoint = (0, 0, 0)
for jointIndex, jointWeight in jointInfluencesForPoint:
    skinnedPoint += skinningTransforms[jointIndex].Transform(skelSpacePoint) * jointWeight

# Result is in skeleton space
```

Expanding the skinning transform:
```
skinnedPoint = Σ(w_i * inv(bindTransforms[i]) * jointSkelTransform[i] * geomBindTransform * localPoint)
```

To get final world-space position (rendering):
```
worldPoint = skinnedPoint * skelLocalToWorldTransform
```

## xformOps on Skinned Primitives: IGNORED

Per the UsdSkel specification:

> "When a primitive is skinned, any transform on the prim authored by way of the
> typical UsdGeomXformable schema has **no effect** on the rendered results.
> Skinned geometry primitives are rendered in skeleton space, rather than
> being transformed back into local gprim space."

This means:
- **Mesh prim's xformOps** (translate, rotate, scale on the mesh or its parent Xforms below SkelRoot) have **no effect** on the final skinned rendering
- The **geomBindTransform** replaces the mesh's xformable transform for skinning purposes
- The **Skeleton prim's world transform** (`skelLocalToWorldTransform`) positions the skinned result in the world

### Example: Layout_Scene.usdz

```
SkelRoot "objectloc_Droid_Standin"  (scale 1,1,1)
  └─ Xform "B1_001"                (scale 0.01, 0.01, 0.01)  ← IGNORED for skinned rendering
      └─ Mesh "B1"                  (extent ~196 units)
           geomBindTransform = rotation-only matrix (no 0.01 scale)
```

The `scale(0.01)` on Xform "B1_001" has **no effect** on the skinned mesh per spec. The mesh appears at 100x size because `geomBindTransform` was not authored to include the 0.01 scale from the parent. This is a file authoring issue, not a renderer bug.

### For baking skinned results to gprim-local space

When baking (as in OpenUSD's `bakeSkinning.cpp`), the mesh's world transform IS used to convert the skeleton-space result back to gprim-local space:
```
localSkinnedPoint = skelSkinnedPoint * skelLocalToWorld * inv(gprimLocalToWorld)
```

## Dual Quaternion Skinning (DQS)

Supported via `primvars:skel:skinningMethod = "DualQuaternion"`. Same transform pipeline as LBS but uses dual quaternion interpolation (preserves volume better around joints).

## Blend Shapes

Applied BEFORE joint skinning: blend shape offsets are added to input positions, then standard skinning (LBS or DQS) is applied.

## TinyUSDZ Tydra Data Export

TinyUSDZ Tydra does NOT compute skinning transforms or apply geomBindTransform to vertices. It exports raw USD data for the renderer to process:

**Per-mesh** (`RenderMesh::joint_and_weights`):
- `geomBindTransform` : matrix4d (as-is from USD, or identity)
- `hasGeomBindTransform` : bool (true if explicitly authored)
- `jointIndices` : int[] (vertexCount × elementSize)
- `jointWeights` : float[] (vertexCount × elementSize)
- `elementSize` : influences per vertex
- `skel_id` : index into `RenderScene::skeletons`

**Per-skeleton** (`SkelHierarchy` → `SkelNode` tree):
- `bind_transform` : world-space bind pose (from `Skeleton.bindTransforms`)
- `rest_transform` : joint-local rest pose (from `Skeleton.restTransforms`, or computed from bindTransforms if missing)
- `joint_id` : index in skeleton's joints array

**Animation** (`AnimationClip`):
- Per-joint TRS animation channels with samplers (time/value arrays)

The renderer (JavaScript/Three.js viewer) is responsible for:
1. Computing `inv(bindTransforms)` (boneInverses)
2. Applying `geomBindTransform` as bind matrix
3. Evaluating the skinning equation per frame

## Three.js Mapping

Three.js skinning vertex shader (column-vector convention):

```
skinned = bindMatrixInverse * Σ(w_i * boneMatrix_i * bindMatrix * pos)
boneMatrix_i = bone.matrixWorld * skeleton.boneInverses[i]
```

Mapping from USD to Three.js:
| USD | Three.js |
|-----|----------|
| `geomBindTransform` | `mesh.bindMatrix` |
| `inv(bindTransforms[i])` | `skeleton.boneInverses[i]` |
| `jointSkelTransform * skelLocalToWorld` | `bone.matrixWorld` |
| (cancelled by AttachedBindMode) | `bindMatrixInverse = inv(mesh.matrixWorld)` |

With **AttachedBindMode** (default), `mesh.matrixWorld` cancels out each frame:

```
world_pos = mesh.matrixWorld * inv(mesh.matrixWorld) * Σ(w_i * boneMatrix_i * bindMatrix * pos)
          = Σ(w_i * bone.matrixWorld * boneInverse_i * geomBindTransform * pos)
```

This matches the OpenUSD equation exactly (adjusted for column-vector convention). The mesh's own xformOps are effectively ignored, consistent with the spec.

EoL.
