# Skinning in UsdSkel

Reference: https://openusd.org/dev/api/_usd_skel__intro.html

## Core Components

* **SkelRoot**: Encapsulation prim. Signals that a subtree contains skinned prims. Any `SkelBindingAPI` bindings without a `UsdSkelRoot` ancestor are invalid.
* **Skeleton**: Encodes joint hierarchy via `joints` (ordered token array of `SdfPath` strings). Stores `bindTransforms` (world-space) and `restTransforms` (joint-local-space).
* **SkelAnimation**: Stores per-joint animation as TRS components in joint-local space. Transform construction order: Scale-Rotate-Translate.
* **UsdSkelBindingAPI**: Connects meshes to skeletons. Defines `skel:skeleton`, `skel:animationSource`, `primvars:skel:jointIndices/jointWeights`, `primvars:skel:geomBindTransform`.

## Terminology

* `bindTransforms`: **World-space** matrices of each joint at bind time (from `Skeleton.bindTransforms`)
* `restTransforms`: **Joint-local-space** matrices of each joint at rest pose (from `Skeleton.restTransforms`). Fallback when `SkelAnimation` does not supply a transform.
* `geomBindTransform`: **World-space** transform of the skinned mesh at bind time (from `primvars:skel:geomBindTransform`). Defaults to identity.
* Sparse: When `SkelAnimation` maps animation to a subset of joints (others fall back to `restTransforms`)

Both `bindTransforms` and `restTransforms` must exist for correct skinning. Neither is automatically computed from the other.

## Transform Spaces

| Space | Description |
|-------|-------------|
| **Joint Local** | Individual joint transforms (animation TRS). Used for blending. |
| **Skeleton Space** | "Object space" of the Skeleton. Does NOT include the Skeleton prim's world positioning. |
| **World Space** | Global scene space. `bindTransforms` and `geomBindTransform` are both in this space. |

## Transform Computations

```
# Joint local -> skeleton space (concatenate parent chain)
jointSkelTransform = jointLocalTransform * parentJointSkelTransform

# Animation TRS -> local transform
jointLocalTransform = MakeTransform(scale, rotation, translation)
  (order: Scale-Rotate-Translate)
```

## Skinning Transform

Per-joint skinning transform (change from bind to animated pose):

```
skinningTransform[i] = inv(bindTransforms[i]) * jointSkelTransform[i]
```

## Linear Blend Skinning (LBS)

```
# Step 1: Transform mesh-local point into bind space
skelSpacePoint = geomBindTransform.Transform(localSpacePoint)

# Step 2: Apply weighted skinning transforms
skinnedPoint = sum_i( w_i * skinningTransform[i].Transform(skelSpacePoint) )

# Result is in skeleton space. For world space:
worldPoint = skinnedPoint * skelLocalToWorldTransform
```

Expanded:
```
skinnedPoint = sum_i( w_i * inv(B_i) * J_i(t) * G * p_local )
```

## xformOps on Skinned Primitives: IGNORED

Per the UsdSkel spec, mesh prim xformOps have **no effect** on skinned rendering. The `geomBindTransform` replaces the mesh's xformable transform. The Skeleton prim's world transform positions the result.

For baking to gprim-local space:
```
localSkinnedPoint = skelSkinnedPoint * skelLocalToWorld * inv(gprimLocalToWorld)
```

## Dual Quaternion Skinning (DQS)

USD selects DQS via `primvars:skel:skinningMethod = "DualQuaternion"` (same
pipeline as LBS but with dual-quaternion interpolation, which preserves volume
better). **Not yet implemented in TinyUSDZ**: the skinning utilities below are
LBS-only and `skinningMethod` is not currently read.

## Blend Shapes

Applied BEFORE joint skinning: offsets added to input positions, then standard skinning applied.

---

## TinyUSDZ Tydra Data Export

The RenderScene conversion exports **raw USD skinning data** (it does not bake
skinned positions); mesh xformOps are kept rather than stripped. Standalone
skinning utilities are provided separately in `scene-access.hh` — see
[Skinning Utilities](#skinning-utilities) — but they are not invoked during the
default export.

**Per-mesh**: `RenderMesh::joint_and_weights` (a `JointAndWeight`) holds
`geomBindTransform` (matrix4d, as-is or identity), `jointIndices` (int[],
vertexCount x elementSize), `jointWeights` (float[], same layout, **not
normalized**), and `elementSize` (influences per vertex). `RenderMesh::skel_id`
indexes `RenderScene::skeletons`.

**Per-skeleton** (`SkelHierarchy` -> `SkelNode` tree, plus flat
`bind_transforms`/`rest_transforms`/`parent_joint_indices` arrays):
- `bind_transform`: world-space bind pose
- `rest_transform`: joint-local rest pose
- `joint_id`: index in skeleton's joints array

**Animation** (`AnimationClip`): per-joint TRS channels with samplers
(time/value arrays).

## Skinning Utilities

`scene-access.hh` provides standalone helpers ported from OpenUSD's
`UsdSkelUtils` (callers run them on the exported raw data):

- `SkinPointsLBS` / `SkinNormalsLBS` — linear blend skinning of points/normals
  (normals via inverse-transpose). **LBS only; no DQS.**
- `ConcatJointTransforms` / `ComputeJointLocalTransforms` — convert between
  local and world joint transforms given topology.
- `SkelMakeTransform` — compose joint-local TRS (Scale-Rotate-Translate).
- `ComputeJointsExtent` / `ComputeSkinnedExtentPadding` /
  `ComputeSkinnedMeshExtent` — fast pivot-based skinned bounds.
- `ExpandConstantInfluencesToVarying` — expand constant influences to per-vertex.

---

## Skinning Evaluation Equations

### Notation

- `p_local`: mesh point in mesh-local space
- `G`: `primvars:skel:geomBindTransform` (world-space)
- `w_i`: skin weight for joint `i`
- `J_i(t)`: joint skeleton-space transform at time `t`
- `B_i`: `bindTransforms[i]` (world-space bind pose)
- `S(t)`: Skeleton prim world transform at time `t`

### USD-Spec Evaluation

```
K_i(t)     = inv(B_i) * J_i(t)
p_skel(t)  = sum_i[ w_i * K_i(t) * G * p_local ]
p_world(t) = S(t) * p_skel(t)
```

Mesh xformOps and parent transforms below SkelRoot are **ignored**.

### Blender Import Evaluation

```
p_world_blender(t) = M_parentAboveSkel(t) * M_parentBelowSkel(t) * M_meshLocal
                      * sum_i[ w_i * inv(B_i) * J_i(t) * G * p_local ]
```

**Key difference**: Blender applies mesh and parent xformOps, which deviates from the USD spec.

### TinyUSDZ / Three.js Evaluation

TinyUSDZ exports the **full USD scene graph** and lets Three.js's world-space `bind()` produce the correct result.

```
W_mesh_bind   = A * M_mesh
W_bone_i_bind = A * M_skel * L_i_bind

p_clip(t) = P * V * sum_i[ w_i * W_bone_i(t) * inv(W_bone_i_bind) * W_mesh_bind * p_local ]
```

Where `A` = shared ancestor chain (scene root -> SkelRoot).

**Why mesh xformOps don't cause double transforms**: Both mesh and bones share the `/SkelRoot` ancestor. The shared prefix cancels in the `bone * inv(bone_bind) * mesh_bind` product.

At bind pose (`L_i(t) = L_i_bind`):
```
p_clip = P * V * W_mesh_bind * p_local
```

### Equivalence to USD spec

| USD spec quantity | Three.js equivalent |
|---|---|
| `G` (geomBindTransform) | `W_mesh_bind` (mesh.matrixWorld at bind) |
| `inv(B_i)` | `inv(W_bone_i_bind)` (calculateInverses) |
| `J_i(t)` | Encoded in `W_bone_i(t)` via scene graph |
| `S(t)` | Implicit in `W_bone_i(t)` ancestor chain |
| Mesh xformOps ignored | Cancel via `inv(W_mesh)` |

### Why this approach works for Three.js

1. **Natural scene graph**: SkinnedMesh/Skeleton designed around world-space `bind()`
2. **AnimationMixer**: Node animations target scene graph nodes by name
3. **AttachedBindMode**: Post-bind ancestor changes handled automatically
4. **No geomBindTransform pass-through**: `W_mesh_bind` serves the same role as `G`

### Maya-USD Import

Maya avoids double transforms by setting `inheritsTransform = false` on skinned mesh transform nodes and applying `geomBindTransform` as the mesh transform.

---

## Practice Notes

- `S(t)` must be evaluated as the Skeleton prim's full local-to-world transform at time `t`, including all ancestor xformOps.
- A transform above a skinned mesh affects final output only if it is part of the Skeleton prim's ancestor chain.
