# Skinning Evaluation Equations 

This note summarizes the **USD spec** evaluation, the **Blender import** evaluation,
and the **TinyUSDZ / Three.js** evaluation for skinned meshes.

## Notation

- `p_local` : mesh point in **mesh-local space** (as stored in the Mesh prim)
- `G` : `primvars:skel:geomBindTransform` on the Mesh prim (world-space bind transform)
- `w_i` : skin weight for joint `i`
- `J_i(t)` : joint **skeleton-space** transform for joint `i` at time `t`
- `B_i` : `bindTransforms[i]` from the Skeleton (world-space bind pose)
- `S(t)` : Skeleton prim **world transform** at time `t` (a.k.a. `skelLocalToWorldTransform`)

- `M_meshLocal` : mesh prim's authored xformOps (mesh-local Xform)
- `M_parentBelowSkel` : any parent Xforms **below** the SkelRoot that are ancestors of the mesh
- `M_parentAboveSkel` : any ancestors of the Skeleton prim (including SkelRoot if it is an ancestor)

---

## USD-Spec Evaluation (Correct)

In USD, **skinned prim xformOps are ignored**. The only transforms participating in skinning are:
`geomBindTransform`, joint transforms, and the Skeleton prim's world transform.

### Skinning transform per joint

```
K_i(t) = inv(B_i) * J_i(t)
```

### Skinned point in skeleton space

```
p_skel(t) = Σ_i [ w_i * K_i(t) * G * p_local ]
```

### Final world-space point

```
p_world(t) = S(t) * p_skel(t)
```

### Expanded

```
p_world(t) = S(t) * Σ_i [ w_i * inv(B_i) * J_i(t) * G * p_local ]
```

**Important:**
- `M_meshLocal` and `M_parentBelowSkel` are **ignored** for skinned rendering.
- `S(t)` is the **world transform of the Skeleton prim**, so it includes *all* of the Skeleton's ancestor Xforms in the USD scene graph (including SkelRoot if it is an ancestor).
- `SkelRoot` itself is just an encapsulation marker; it does **not** change the skinning math beyond being part of the Skeleton prim's ancestor chain.

---

## Blender Import Evaluation (Observed)

Blender (for this USD import) applies the **object transform chain** on top of armature deformation.
So transforms authored on the mesh prim and its parents **below the SkelRoot** are applied in addition to skinning.

### Blender-style evaluation

```
p_world_blender(t) = M_parentAboveSkel(t) * M_parentBelowSkel(t) * M_meshLocal * p_skin(t)
```

Where `p_skin(t)` is the armature-deformed point Blender computes from the Skeleton animation.
This is effectively:

```
p_skin(t) = Σ_i [ w_i * K_i(t) * G * p_local ]
```

So the full Blender-style equation becomes:

```
p_world_blender(t) = M_parentAboveSkel(t) * M_parentBelowSkel(t) * M_meshLocal
                      * Σ_i [ w_i * inv(B_i) * J_i(t) * G * p_local ]
```

**Key difference vs USD spec:**
- Blender applies `M_meshLocal` and `M_parentBelowSkel` (mesh and parent XformOps)
- USD spec does **not** apply those transforms to skinned geometry

---

## TinyUSDZ / Three.js Evaluation

TinyUSDZ does **not** strip mesh xformOps from skinned prims.  Instead it exports the
**full USD scene graph** (all transforms on every node) and lets Three.js's world-space
`bind()` mechanism produce the correct result.

### Additional Notation

- `W_mesh`  : `mesh.matrixWorld` — the SkinnedMesh's world transform in Three.js
               (product of all ancestor local matrices from scene root to mesh)
- `W_bone_i`: `bone.matrixWorld` — bone `i`'s world transform
               (product of all ancestor local matrices from scene root to bone)
- Subscript `_bind` denotes the value captured at bind time (before animation starts).
- `W_mesh` includes `/SkelRoot`, `/SkinnedMeshXf`, and `/SkinnedMesh` xformOps.
- `W_bone_i` includes `/SkelRoot`, `/ModelXform`, `/Skeleton` xformOps,
  plus the bone's rest-transform chain.

### How bind() works (no custom arguments)

`mesh.bind(skeleton)` internally:

1. `calculateInverses()` → `boneInverses[i] = inv(W_bone_i_bind)`
2. `bindMatrix = W_mesh_bind`

No USD `geomBindTransform` or `bindTransforms` are passed — Three.js computes
everything from its own scene graph in world space.

### Per-bone skinning matrix

```
boneMatrix_i(t) = W_bone_i(t) * inv(W_bone_i_bind)
```

At bind pose `W_bone_i(t) == W_bone_i_bind`, so `boneMatrix_i = I`.

### Skinned point (mesh-local space, computed in vertex shader)

```
p_skinned(t) = inv(W_mesh) * Σ_i [ w_i * W_bone_i(t) * inv(W_bone_i_bind) * W_mesh_bind * p_local ]
```

With `AttachedBindMode` (default), `inv(W_mesh)` is recomputed every frame.

### Final clip-space point (vertex shader output)

```
p_clip(t) = P * V * W_mesh * p_skinned(t)
           = P * V * Σ_i [ w_i * W_bone_i(t) * inv(W_bone_i_bind) * W_mesh_bind * p_local ]
```

The outer `W_mesh * inv(W_mesh)` cancels, leaving the weighted sum in world space.

### Why mesh xformOps do not cause double transforms

Both mesh and bones share the `/SkelRoot` ancestor.  Let:

- `A`        = shared ancestor chain (scene root → `/SkelRoot`)
- `M_mesh`   = mesh-branch local chain (`/SkinnedMeshXf` · `/SkinnedMesh` xformOps)
- `M_skel`   = skeleton-branch local chain (`/ModelXform` · `/Skeleton` xformOps)
- `L_i`      = bone `i`'s local rest-transform chain within the Skeleton

Then:

```
W_mesh_bind   = A · M_mesh
W_bone_i_bind = A · M_skel · L_i_bind
```

Substituting into the skinned formula:

```
p_clip = P * V * Σ[ w_i * A · M_skel · L_i(t) * inv(L_i_bind) * inv(M_skel) * inv(A) * A · M_mesh * p_local ]
       = P * V * Σ[ w_i * A · M_skel · L_i(t) * inv(L_i_bind) * inv(M_skel) * M_mesh * p_local ]
```

The shared ancestor `A` appears once (as the outermost world-space placement).
`M_mesh` and `M_skel` do not need to be equal — they conjugate the bone animation but
produce the correct world-space result because `inv(M_skel) * M_mesh` maps mesh-local
vertices into skeleton-local space (serving the same role as `inv(S) * G` in the USD spec).

At **bind pose** (`L_i(t) = L_i_bind`):

```
p_clip = P * V * A · M_mesh * p_local = P * V * W_mesh_bind * p_local
```

The mesh renders at its scene-graph position — correct.

### Equivalence to USD spec

The Three.js formula is equivalent to the USD formula when the transforms are
consistent. Mapping between the two:

| USD spec quantity | Three.js equivalent |
|---|---|
| `G` (geomBindTransform) | `W_mesh_bind` (mesh.matrixWorld at bind) |
| `inv(B_i)` (inverse bind pose) | `inv(W_bone_i_bind)` (calculateInverses) |
| `J_i(t)` (joint skeleton-space) | Encoded in `W_bone_i(t)` via scene graph |
| `S(t)` (skeleton world transform) | Implicit in `W_bone_i(t)` ancestor chain |
| Mesh xformOps ignored | Mesh xformOps present but cancel via `inv(W_mesh)` |

The key difference: the USD spec operates in skeleton-local space then applies `S(t)`,
while Three.js operates entirely in world space.  Both produce the same `p_world`.

### Why this approach is preferred over literal USD spec for Three.js

1. **Natural scene graph**: Three.js SkinnedMesh/Skeleton are designed around world-space
   `bind()`.  Stripping mesh xformOps would fight the framework.
2. **AnimationMixer integration**: node animations (time-sampled xformOps on ancestors)
   target scene graph nodes by name.  Preserving the full hierarchy makes skeletal and
   node animations work through a single unified system.
3. **AttachedBindMode**: any post-bind ancestor change (Z-up toggle, user scene
   manipulation) is handled automatically because `inv(W_mesh)` is recomputed per frame.
4. **No geomBindTransform pass-through needed**: `W_mesh_bind` captures the mesh's
   world-space bind position, which serves the same role as `G` without mixing
   USD skeleton-local matrices into Three.js world-space math.

---

## Summary

- **USD spec:** only `geomBindTransform`, joint transforms, and Skeleton world transform affect skinned output.  Mesh xformOps are ignored.
- **Blender import:** applies mesh + parent Xforms in addition to skinning, which can create double transforms.
- **TinyUSDZ / Three.js:** preserves full scene graph with all xformOps.  Three.js `bind()` captures world-space matrices for both mesh and bones; shared ancestors cancel naturally.  Produces correct results equivalent to the USD spec.

---

## Addendum: Generic Prim Chain with Time-Sampled Xforms

This addendum describes the transform order using **generic prim names** and
explicitly separates time-sampled `xformOp`s from skinning.

### Generic Prim Hierarchy

```
/Root
  /SceneXform           (xformOps, may be time-sampled)
    /SkelRoot           (encapsulation prim; may have xformOps)
      /ModelXform       (xformOps, may be time-sampled)
        /Skeleton       (UsdSkelSkeleton; has world transform S(t))
          /SkelAnim     (UsdSkelAnimation; TRS samples per joint)
      /SkinnedMeshXf    (xformOps on the mesh prim or mesh's parent)
        /SkinnedMesh    (UsdGeomMesh with SkelBindingAPI)
```

### USD-Spec Evaluation (Generic Order)

```
K_i(t)      = inv(B_i) * J_i(t)
p_skel(t)   = Σ_i [ w_i * K_i(t) * G * p_local ]
p_world(t)  = S(t) * p_skel(t)
```

Notes:
- `S(t)` is the **world transform of /Skeleton** and includes all ancestor xforms
  up to the stage root (e.g., `/Root`, `/SceneXform`, `/SkelRoot`, `/ModelXform`).
- `SkinnedMeshXf` (mesh-local xformOps) and any ancestor xforms **below the SkelRoot**
  are **ignored** for skinned rendering.

### Blender Import Evaluation (Generic Order)

```
p_world_blender(t) =
  M_parentAboveSkel(t) * M_parentBelowSkel(t) * M_meshLocal
  * Σ_i [ w_i * inv(B_i) * J_i(t) * G * p_local ]
```

Notes:
- `M_parentAboveSkel` corresponds to ancestor xforms of `/Skeleton`
  (e.g., `/Root`, `/SceneXform`, `/SkelRoot`, `/ModelXform`).
- `M_parentBelowSkel` corresponds to ancestor xforms of `/SkinnedMesh`
  that are **below** `/SkelRoot` (e.g., `/SkinnedMeshXf` or other mesh parents).
- Blender applies `M_meshLocal` and `M_parentBelowSkel`, which is the key deviation
  from the USD spec.

### TinyUSDZ / Three.js Evaluation (Generic Order)

```
W_mesh_bind   = Root * SceneXform * SkelRoot * SkinnedMeshXf * SkinnedMesh_xform
W_bone_i_bind = Root * SceneXform * SkelRoot * ModelXform * Skeleton_xform * L_i_bind

p_clip(t) = P * V * Σ_i [ w_i * W_bone_i(t) * inv(W_bone_i_bind) * W_mesh_bind * p_local ]
```

Notes:
- All ancestor xforms are preserved in the Three.js scene graph — nothing is stripped.
- `W_mesh_bind` and `W_bone_i_bind` share the prefix `Root * SceneXform * SkelRoot`,
  which cancels in the `bone * inv(bone_bind) * mesh_bind` product.
- `/SkinnedMeshXf` xformOps are present in `W_mesh_bind` (unlike the USD spec where
  they are ignored), but they do not cause double transforms because the formula
  self-consistently uses `inv(W_bone_i_bind)` (not USD `inv(B_i)`).

---

## Addendum: Maya-USD Import Behavior

Maya-USD avoids double transforms by **disabling inherited transforms** on skinned
mesh transform nodes and then **applying `geomBindTransform` as the mesh transform**.
This matches the USD spec expectation that mesh xformOps should not affect the final
skinned result.

Key behavior in `<maya-usd>/lib/mayaUsd/fileio/translators/translatorSkel.cpp`:

- `_ConfigureSkinnedObjectTransform()` sets `inheritsTransform = false` on the Maya
  transform node. This prevents parent xforms from affecting the skinned mesh.
- It then reads `skinningQuery.GetGeomBindTransform()` and sets that matrix onto the
  transform node as the mesh's world-space bind placement.

Relevant code path:
- `_ConfigureSkinnedObjectTransform` in `<maya-usd>/lib/mayaUsd/fileio/translators/translatorSkel.cpp`
- Called from `UsdMayaTranslatorSkel::CreateSkinCluster`, which wires the skin cluster
  after configuring the transform.

---

## Practice Notes: Time-Sampled Xforms and `S(t)`

- `S(t)` must be evaluated as the Skeleton prim's full local-to-world transform at time `t`.
- This means all ancestor xformOps of the Skeleton prim are included in `S(t)`, including
  any time-sampled xformOps.
- In practice, evaluate `S(t)` per frame/time-sample used for skinning or rendering.

### Common confusion: "Geom parent xform"

- A transform above a skinned mesh affects final skinned output only if it is part of the
  Skeleton prim's ancestor chain (i.e., contributes to `S(t)`).
- A mesh-only parent transform that is not in the Skeleton ancestor chain is not part of
  USD skinned rendering evaluation.
