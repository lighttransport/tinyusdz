# Skinning Notes

## UsdSkel Spec: Key Rules for This Viewer

Per the UsdSkel specification (see `doc/skinning.md` for full details):

1. **xformOps on skinned prims are IGNORED** for rendered results. The mesh's own transform hierarchy has no effect on the skinned output. Only `geomBindTransform` and the Skeleton's world transform matter.

2. **geomBindTransform** is the world-space transform of the mesh at bind time. It replaces the mesh's xformable transform for skinning purposes. Defaults to identity if not authored.

3. **bindTransforms** (on Skeleton) are world-space transforms of each joint at bind time.

4. The skinning equation produces results in **skeleton space**. The Skeleton prim's world transform (`skelLocalToWorld`) positions the result in the world.

### USD → Three.js Mapping

| USD Concept | Three.js Equivalent |
|-------------|---------------------|
| `geomBindTransform` | `mesh.bindMatrix` |
| `inv(Skeleton.bindTransforms[i])` | `skeleton.boneInverses[i]` |
| `jointSkelTransform * skelLocalToWorld` | `bone.matrixWorld` |
| (cancelled by AttachedBindMode) | `bindMatrixInverse = inv(mesh.matrixWorld)` |

### The Skinning Equation

USD (column-vector convention):
```
skinnedPoint = Σ(w_i * inv(bindTransforms[i]) * jointSkelTransform[i] * geomBindTransform * localPoint)
```

Three.js with AttachedBindMode:
```
world_pos = Σ(w_i * bone.matrixWorld * boneInverse_i * bindMatrix * pos)
```

These are equivalent. With AttachedBindMode, `inv(mesh.matrixWorld)` replaces `bindMatrixInverse` each frame, so mesh xformOps cancel out — consistent with the USD spec rule.

## Z-up to Y-up Conversion for Skinned Meshes

### Approach: characterGroup.rotation.x

With AttachedBindMode, rotating `characterGroup` (ancestor of both mesh and bones) applies the rotation R to both `bone.matrixWorld` and `mesh.matrixWorld`. Since `inv(R*M) * R = inv(M)`, the R cancels out in the skinning equation and only affects the final `mesh.matrixWorld` multiplication in the vertex shader — correctly rotating the entire skinned result.

### Math (Skinned Mesh)

Three.js skinning vertex shader:
```
skinned = bindMatrixInverse * sum(w_i * boneMatrix_i * bindMatrix * position)
boneMatrix_i = bone_i.matrixWorld * boneInverse_i
```

With AttachedBindMode, `bindMatrixInverse = inv(mesh.matrixWorld)` each frame.
When ancestor R is applied, both `bone.matrixWorld` and `mesh.matrixWorld` get R prefix:
```
world_pos = (R * mesh.oldMatrixWorld) * inv(R * mesh.oldMatrixWorld)
            * sum(w_i * (R * bone.oldMatrixWorld) * boneInverse_i * bindMatrix * pos)
          = sum(w_i * R * bone.oldMatrixWorld * boneInverse_i * bindMatrix * pos)
          = R * (original result)
```

### Toggle Support

Toggle on: `characterGroup.rotation.x = -Math.PI / 2`
Toggle off: `characterGroup.rotation.x = 0`

No per-mesh adjustments needed with AttachedBindMode.

### Related Files

- `skin-anim.js`: Main skeletal animation viewer
- `doc/skinning.md`: UsdSkel spec reference and full equation derivation
- `buildSkeletonFromUSD()`: Builds skeleton and computes inverse bind matrices
- `processUSDScene()`: Sets up skinned meshes with geomBindTransform as bindMatrix
- `toggleUpAxisConversion()`: Toggles Z-up to Y-up via characterGroup rotation

### References

- [UsdSkel Introduction](https://openusd.org/dev/api/_usd_skel__intro.html)
- [UsdSkel Schemas In-Depth](https://openusd.org/dev/api/_usd_skel__schemas.html) — geomBindTransform and xformOp behavior
- Three.js SkinnedMesh skinning formula
- USD bindTransforms = world-space, restTransforms = joint-local-space
- geomBindTransform = mesh's world transform at bind time → Three.js `bindMatrix`
