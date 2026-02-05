# Skinning Notes

## TODO/FIXME: Z-up to Y-up Conversion for Skinned Meshes

### Issue

When a USD file has `upAxis = "Z"` (Z-up coordinate system), the viewer cannot apply the standard Z-up to Y-up rotation to skinned meshes. Rotating the scene root breaks skinning because:

1. The skeleton's inverse bind matrices are computed from the original bind transforms (in Z-up space)
2. When the scene root is rotated, bone world matrices change to include the rotation
3. The skinning formula `boneMatrix = bone.matrixWorld * boneInverse` no longer produces identity at bind pose
4. This causes the mesh to deform incorrectly (mesh and skeleton become misaligned)

### Current Workaround

For skinned meshes, the Z-up to Y-up conversion is **disabled**. The model is displayed in its original Z-up coordinate system. The console outputs:

```
[processUSDScene] Skipping Z-up to Y-up rotation (has skinned meshes - rotation breaks skinning)
[processUSDScene] Model is in original Z-up coordinate system. Use camera orbit to view from different angles.
```

### Proper Fix Required

To properly support Z-up to Y-up conversion for skinned meshes, one of the following approaches is needed:

1. **Bake transformation at load time**: Transform mesh vertex positions, skeleton bind/rest transforms, inverse bind matrices, and geomBindTransform consistently before setting up skinning. This is complex because all these matrices are interdependent.

2. **Transform in the loader (C++/WASM side)**: Apply coordinate transformation when parsing USD data, before exposing to JavaScript. This would be the cleanest solution.

3. **Use transformed bind matrices**: When computing inverse bind matrices, pre-multiply the bind transforms by the Z-up to Y-up rotation. Also transform geomBindTransform accordingly. Initial attempts at this caused mesh deformation issues.

### Related Files

- `skin-anim.js`: Main skeletal animation viewer
- `buildSkeletonFromUSD()`: Builds skeleton and computes inverse bind matrices
- `processUSDScene()`: Handles Z-up to Y-up detection and (for non-skinned meshes) rotation

### References

- Three.js SkinnedMesh skinning formula: `skinMatrix = bindMatrix * (weighted boneMatrices) * bindMatrixInverse`
- USD Skeleton specification: bindTransforms are world-space, restTransforms are local-space
- geomBindTransform: mesh's world transform at bind time
