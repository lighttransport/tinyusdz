# Skinning Evaluation Equations (General Prim Names)

This note summarizes the **USD spec** evaluation and the **Blender import** evaluation for skinned meshes.
The equations are written with **generic prim names** so they can be applied to any skinned asset.

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

## Summary

- **USD spec:** only `geomBindTransform`, joint transforms, and Skeleton world transform affect skinned output.
- **Blender import:** applies mesh + parent Xforms in addition to skinning, which can create double transforms.

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

---

## Addendum: Maya-USD Import Behavior (Evidence from Local Source)

Maya-USD avoids double transforms by **disabling inherited transforms** on skinned
mesh transform nodes and then **applying `geomBindTransform` as the mesh transform**.
This matches the USD spec expectation that mesh xformOps should not affect the final
skinned result.

Key behavior in `../maya-usd/lib/mayaUsd/fileio/translators/translatorSkel.cpp`:

- `_ConfigureSkinnedObjectTransform()` sets `inheritsTransform = false` on the Maya
  transform node. This prevents parent xforms from affecting the skinned mesh.
- It then reads `skinningQuery.GetGeomBindTransform()` and sets that matrix onto the
  transform node as the mesh's world-space bind placement.

Relevant code path:
- `_ConfigureSkinnedObjectTransform` in `../maya-usd/lib/mayaUsd/fileio/translators/translatorSkel.cpp`
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
