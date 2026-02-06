# Skinning Notes

## Z-up to Y-up Conversion for Skinned Meshes

### Approach: Per-mesh, No Scene Root Rotation

When `upAxis = "Z"`, instead of rotating the scene root (which breaks skinning), we handle each mesh type differently:

- **Static (non-skinned) meshes**: Rotate geometry directly via `geometry.applyMatrix4(R)`
- **Skinned meshes**: Skip geometry rotation. Instead, prepend R to `bindMatrixInverse` so the skinning output is rotated to Y-up.

No scene root rotation is used. No auto-detection heuristics.

### Math (Skinned Mesh)

Three.js skinning vertex shader:
```
skinned = bindMatrixInverse * sum(w_i * boneMatrix_i * bindMatrix * position)
boneMatrix_i = bone_i.matrixWorld * boneInverse_i
```

By setting `bindMatrixInverse' = R * bindMatrixInverse` (all else unchanged):
```
skinned = R * bindMatrixInverse * sum(w_i * boneMatrix_i) * bindMatrix * position
        = R * (original Z-up skinned result)
        = Y-up result
```

At bind pose, `boneMatrix_i = I` still holds (boneInverses and bone.matrixWorld are untouched), so:
```
skinned = R * bindMatrixInverse * bindMatrix * position = R * position
```
Which correctly displays a Y-up position.

### Implementation (in `processUSDScene`)

```javascript
const R = new THREE.Matrix4().makeRotationX(-Math.PI / 2);

characterGroup.traverse((child) => {
    if (child.isSkinnedMesh) {
        // Skinned: adjust bindMatrixInverse to rotate output
        child.bindMatrixInverse.premultiply(R);
    } else if (child.isMesh && child.geometry) {
        // Static: rotate geometry directly
        child.geometry.applyMatrix4(R);
    }
});
```

### Toggle Support

Toggle on: `bindMatrixInverse.premultiply(R)` + `geometry.applyMatrix4(R)`
Toggle off: `bindMatrixInverse.premultiply(Rinv)` + `geometry.applyMatrix4(Rinv)`

### Related Files

- `skin-anim.js`: Main skeletal animation viewer
- `buildSkeletonFromUSD()`: Builds skeleton and computes inverse bind matrices
- `processUSDScene()`: Applies per-mesh Z-up to Y-up conversion
- `toggleUpAxisConversion()`: Toggles the conversion on/off at runtime

### References

- Three.js SkinnedMesh skinning formula
- USD Skeleton specification: bindTransforms are world-space, restTransforms are local-space
- geomBindTransform: mesh's world transform at bind time (Three.js `bindMatrix`)
