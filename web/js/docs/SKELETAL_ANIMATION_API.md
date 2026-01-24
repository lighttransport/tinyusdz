# USD Skeletal Animation API for Three.js

This document describes the enhanced WASM binding API and Three.js helper library for working with skeletal animations from USD files.

## Table of Contents

1. [WASM Binding API](#wasm-binding-api)
2. [Three.js Helper Library](#threejs-helper-library)
3. [Quick Start Example](#quick-start-example)
4. [Advanced Usage](#advanced-usage)
5. [Data Structures](#data-structures)

---

## WASM Binding API

The TinyUSDZ WASM binding provides low-level access to USD skeleton and animation data.

### Skeleton Methods

#### `numSkeletons(): number`

Returns the total number of skeletons in the loaded USD scene.

```javascript
const usd = await loader.load('character.usdz');
const numSkeletons = usd.numSkeletons();
console.log(`Found ${numSkeletons} skeletons`);
```

#### `getSkeleton(skelId: number): Object`

Retrieves complete skeleton hierarchy for a given skeleton ID.

**Returns:**
```javascript
{
  id: number,                // Skeleton ID
  prim_name: string,         // USD prim name
  abs_path: string,          // Absolute USD path
  display_name: string,      // Display name
  anim_id: number,           // Default animation ID (-1 if none)
  root_node: {               // Root joint node
    joint_path: string,      // USD joint path
    joint_name: string,      // Joint name
    joint_id: number,        // Joint index
    bind_transform: Float64Array,  // 4x4 matrix (16 elements)
    rest_transform: Float64Array,  // 4x4 matrix (16 elements)
    children: [...],         // Recursive child joints
  }
}
```

**Example:**
```javascript
const skeleton = usd.getSkeleton(0);
console.log('Skeleton:', skeleton.prim_name);
console.log('Root joint:', skeleton.root_node.joint_name);
console.log('Default animation:', skeleton.anim_id);
```

#### `getAllSkeletons(): Array<Object>`

Returns all skeletons in the scene.

```javascript
const skeletons = usd.getAllSkeletons();
skeletons.forEach((skel, i) => {
  console.log(`Skeleton ${i}: ${skel.prim_name}`);
});
```

#### `getSkeletonJointsFlat(skelId: number): Object`

Returns skeleton data in a flattened, optimized format for Three.js.

**Returns:**
```javascript
{
  num_joints: number,
  joint_names: string[],          // Joint names array
  joint_paths: string[],          // Joint paths array
  joint_ids: Int32Array,          // Joint IDs
  parent_indices: Int32Array,     // Parent joint indices (-1 for root)
  bind_matrices: Float64Array,    // Flattened 4x4 matrices (num_joints * 16)
  rest_matrices: Float64Array,    // Flattened 4x4 matrices (num_joints * 16)
}
```

**Example:**
```javascript
const flatSkeleton = usd.getSkeletonJointsFlat(0);
console.log(`${flatSkeleton.num_joints} joints`);
for (let i = 0; i < flatSkeleton.num_joints; i++) {
  console.log(`Joint ${i}: ${flatSkeleton.joint_names[i]} (parent: ${flatSkeleton.parent_indices[i]})`);
}
```

### Animation Methods

Existing animation methods are already exposed:

#### `numAnimations(): number`

Returns the number of animation clips.

#### `getAnimation(animId: number): Object`

Returns animation clip data including channels and samplers.

**Returns:**
```javascript
{
  id: number,
  name: string,
  prim_name: string,
  abs_path: string,
  duration: number,          // Duration in seconds
  channels: [                // Animation channels
    {
      sampler: number,       // Index into samplers array
      target_node: number,   // Target node index (for node animations)
      skeleton_id: number,   // Skeleton ID (for skeletal animations)
      joint_id: number,      // Joint ID within skeleton
      target_type: string,   // "SceneNode" or "SkeletonJoint"
      path: string,          // "Translation", "Rotation", "Scale", or "Weights"
    },
    ...
  ],
  samplers: [                // Keyframe data
    {
      times: Float32Array,   // Keyframe times
      values: Float32Array,  // Keyframe values (flattened)
      interpolation: string, // "LINEAR", "STEP", or "CUBICSPLINE"
    },
    ...
  ]
}
```

#### `getAnimationInfo(animId: number): Object`

Returns animation summary without full data.

#### `getAllAnimations(): Array<Object>`

Returns all animation clips.

### Mesh Skinning Data

Existing mesh methods include skinning data:

#### `getMesh(meshId: number): Object`

Returns mesh data including skinning attributes.

**Skinning fields:**
```javascript
{
  skel_id: number,           // Skeleton ID this mesh is bound to (-1 if not skinned)
  jointIndices: Int32Array,  // Joint indices per vertex (size: vertices * influencesPerVertex)
  jointWeights: Float32Array,// Joint weights per vertex (size: vertices * influencesPerVertex)
  geomBindTransform: Float64Array, // 4x4 bind transform matrix (16 elements)
  // ... other mesh data (points, normals, texcoords, etc.)
}
```

---

## Three.js Helper Library

The `USDSkeletalHelper.js` module provides high-level utilities to convert USD data to Three.js format.

### Import

```javascript
import {
  createThreeSkeletonFromUSD,
  createThreeSkeletonFromFlat,
  createThreeAnimationClip,
  createSkinnedMesh,
  addSkinningAttributes,
  createSkinnedMeshFromUSD,
  playAnimation
} from './tinyusdz/USDSkeletalHelper.js';
```

### Core Functions

#### `createThreeSkeletonFromUSD(usdSkeleton, options): THREE.Skeleton`

Creates a Three.js Skeleton from hierarchical USD skeleton data.

**Parameters:**
- `usdSkeleton` - Skeleton data from `usd.getSkeleton(id)`
- `options.useBindPose` (boolean, default: true) - Use bind pose instead of rest pose

**Returns:** `THREE.Skeleton`

**Example:**
```javascript
const usdSkeleton = usd.getSkeleton(0);
const skeleton = createThreeSkeletonFromUSD(usdSkeleton, { useBindPose: true });
```

#### `createThreeSkeletonFromFlat(flatSkeleton, options): THREE.Skeleton`

Creates a Three.js Skeleton from flattened USD skeleton data (more efficient).

**Parameters:**
- `flatSkeleton` - Skeleton data from `usd.getSkeletonJointsFlat(id)`
- `options.useBindPose` (boolean, default: true)

**Returns:** `THREE.Skeleton`

**Example:**
```javascript
const flatSkel = usd.getSkeletonJointsFlat(0);
const skeleton = createThreeSkeletonFromFlat(flatSkel);
```

#### `createThreeAnimationClip(usdAnimation, skeleton, options): THREE.AnimationClip`

Converts USD animation to Three.js AnimationClip.

**Parameters:**
- `usdAnimation` - Animation data from `usd.getAnimation(id)`
- `skeleton` - Three.js Skeleton to target
- `options.fps` (number, default: 24) - Frames per second for time conversion

**Returns:** `THREE.AnimationClip`

**Example:**
```javascript
const usdAnim = usd.getAnimation(0);
const clip = createThreeAnimationClip(usdAnim, skeleton, { fps: 24 });
```

#### `addSkinningAttributes(geometry, usdMesh, influencesPerVertex): THREE.BufferGeometry`

Adds skinning attributes to a BufferGeometry.

**Parameters:**
- `geometry` - Target Three.js BufferGeometry
- `usdMesh` - USD mesh data with jointIndices and jointWeights
- `influencesPerVertex` (number, default: 4) - Bone influences per vertex

**Returns:** Modified `THREE.BufferGeometry`

**Example:**
```javascript
const geometry = new THREE.BufferGeometry();
geometry.setAttribute('position', new THREE.Float32BufferAttribute(usdMesh.points, 3));
addSkinningAttributes(geometry, usdMesh, 4);
```

#### `createSkinnedMesh(geometry, material, skeleton, usdMesh): THREE.SkinnedMesh`

Creates a Three.js SkinnedMesh.

**Parameters:**
- `geometry` - BufferGeometry with skinning attributes
- `material` - Material for the mesh
- `skeleton` - Three.js Skeleton
- `usdMesh` - Original USD mesh data (for metadata)

**Returns:** `THREE.SkinnedMesh`

**Example:**
```javascript
const material = new THREE.MeshStandardMaterial({ skinning: true });
const skinnedMesh = createSkinnedMesh(geometry, material, skeleton, usdMesh);
scene.add(skinnedMesh);
```

#### `createSkinnedMeshFromUSD(usd, meshId, skelId, animId, options): Object`

**All-in-one helper** that creates a complete skinned mesh with animation from USD data.

**Parameters:**
- `usd` - TinyUSDZ loader instance
- `meshId` - Mesh ID
- `skelId` - Skeleton ID
- `animId` - Animation ID (optional, can be undefined)
- `options.material` - Three.js material to use
- `options.fps` (number, default: 24) - FPS for animation

**Returns:**
```javascript
{
  mesh: THREE.SkinnedMesh,
  skeleton: THREE.Skeleton,
  animationClip: THREE.AnimationClip | null,
  geometry: THREE.BufferGeometry,
  material: THREE.Material
}
```

**Example:**
```javascript
const result = createSkinnedMeshFromUSD(usd, 0, 0, 0, {
  material: new THREE.MeshStandardMaterial({ skinning: true, color: 0x3399ff }),
  fps: 24
});

scene.add(result.mesh);

if (result.animationClip) {
  const mixer = new THREE.AnimationMixer(result.mesh);
  const action = mixer.clipAction(result.animationClip);
  action.play();
}
```

#### `playAnimation(mesh, clip, options): THREE.AnimationMixer`

Helper to create a mixer and play animation.

**Parameters:**
- `mesh` - SkinnedMesh
- `clip` - AnimationClip
- `options.loop` (boolean, default: true)
- `options.timeScale` (number, default: 1)

**Returns:** `THREE.AnimationMixer`

**Example:**
```javascript
const mixer = playAnimation(mesh, animationClip, { loop: true, timeScale: 1.0 });

// In animation loop:
function animate() {
  const delta = clock.getDelta();
  mixer.update(delta);
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}
```

---

## Quick Start Example

```javascript
import * as THREE from 'three';
import { TinyUSDZLoader } from './tinyusdz/TinyUSDZLoader.js';
import { createSkinnedMeshFromUSD, playAnimation } from './tinyusdz/USDSkeletalHelper.js';

// Setup scene
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, window.innerWidth / window.innerHeight, 0.1, 1000);
const renderer = new THREE.WebGLRenderer();
// ... setup renderer, lights, etc.

// Load USD file
const loader = new TinyUSDZLoader();
await loader.init();

const usd = await new Promise((resolve, reject) => {
  loader.load('character.usdz', resolve, null, reject);
});

// Get first skinned mesh
const mesh = usd.getMesh(0);

if (mesh.skel_id >= 0) {
  // Create skinned mesh with animation
  const result = createSkinnedMeshFromUSD(
    usd,
    0,              // mesh ID
    mesh.skel_id,   // skeleton ID
    0,              // animation ID
    {
      material: new THREE.MeshStandardMaterial({ skinning: true }),
      fps: 24
    }
  );

  scene.add(result.mesh);

  // Play animation
  const mixer = playAnimation(result.mesh, result.animationClip);

  // Animation loop
  const clock = new THREE.Clock();
  function animate() {
    mixer.update(clock.getDelta());
    renderer.render(scene, camera);
    requestAnimationFrame(animate);
  }
  animate();
}
```

---

## Advanced Usage

### Manual Skeleton Creation

```javascript
// Get skeleton data
const usdSkeleton = usd.getSkeleton(0);

// Create Three.js skeleton
const skeleton = createThreeSkeletonFromUSD(usdSkeleton);

// Access bones
skeleton.bones.forEach((bone, index) => {
  console.log(`Bone ${index}: ${bone.name}`);
  console.log(`  Position: ${bone.position.toArray()}`);
  console.log(`  Rotation: ${bone.quaternion.toArray()}`);
  console.log(`  Joint ID: ${bone.userData.joint_id}`);
});
```

### Custom Material with Skinning

```javascript
const material = new THREE.MeshStandardMaterial({
  color: 0x3399ff,
  skinning: true,  // REQUIRED for skinning
  roughness: 0.5,
  metalness: 0.3,
  map: texture
});
```

### Animation Control

```javascript
const mixer = new THREE.AnimationMixer(skinnedMesh);
const action = mixer.clipAction(animationClip);

// Control playback
action.play();
action.pause();
action.stop();

// Set time
action.time = 2.0;  // Jump to 2 seconds

// Set speed
action.timeScale = 0.5;  // Half speed

// Loop options
action.setLoop(THREE.LoopRepeat, Infinity);  // Loop forever
action.setLoop(THREE.LoopOnce, 1);           // Play once

// Blending
action.fadeIn(0.5);   // Fade in over 0.5 seconds
action.fadeOut(0.5);  // Fade out over 0.5 seconds
```

### Skeleton Visualization

```javascript
// Add skeleton helper for debugging
const skeletonHelper = new THREE.SkeletonHelper(skinnedMesh);
skeletonHelper.material.linewidth = 2;
scene.add(skeletonHelper);
```

### Multiple Animations

```javascript
// Load multiple animations
const animations = usd.getAllAnimations();

// Create clips for each
const clips = animations
  .filter(anim => anim.has_skeletal_animation)
  .map(anim => createThreeAnimationClip(anim, skeleton));

// Play specific animation
const mixer = new THREE.AnimationMixer(skinnedMesh);
const walkAction = mixer.clipAction(clips[0]);
const runAction = mixer.clipAction(clips[1]);

// Blend between animations
walkAction.play();
runAction.play();
walkAction.fadeOut(1.0);
runAction.fadeIn(1.0);
```

---

## Data Structures

### Skeleton Hierarchy

USD skeletons are hierarchical trees of joints:

```
Root Joint (e.g., "Hips")
├── Spine
│   ├── Chest
│   │   ├── LeftShoulder
│   │   │   ├── LeftUpperArm
│   │   │   └── ...
│   │   └── RightShoulder
│   └── Neck
│       └── Head
├── LeftUpperLeg
│   └── LeftLowerLeg
│       └── LeftFoot
└── RightUpperLeg
    └── RightLowerLeg
        └── RightFoot
```

### Transform Matrices

USD provides two key transform matrices for each joint:

1. **Bind Transform** (`bind_transform`): The transform from joint space to bind space (used for skinning)
2. **Rest Transform** (`rest_transform`): The joint's rest/neutral pose

Matrices are stored as **column-major** 4x4 matrices (16 doubles):
```
[m00, m10, m20, m30,  // Column 0
 m01, m11, m21, m31,  // Column 1
 m02, m12, m22, m32,  // Column 2
 m03, m13, m23, m33]  // Column 3
```

### Animation Keyframes

Animation data consists of:
- **Times array**: Keyframe timestamps (usually in frame numbers)
- **Values array**: Flattened keyframe values
  - Translation: `[x0,y0,z0, x1,y1,z1, ...]` (3 floats per frame)
  - Rotation: `[x0,y0,z0,w0, x1,y1,z1,w1, ...]` (4 floats per frame as quaternion)
  - Scale: `[x0,y0,z0, x1,y1,z1, ...]` (3 floats per frame)

### Skinning Attributes

For each vertex:
- **Joint Indices**: Up to 4 bone indices that influence the vertex
- **Joint Weights**: Corresponding weights (should sum to 1.0)

Example for vertex 0 with 4 influences:
```javascript
jointIndices: [2, 5, 7, 0]  // Influenced by joints 2, 5, 7, 0
jointWeights: [0.5, 0.3, 0.15, 0.05]  // Weights sum to 1.0
```

---

## Best Practices

1. **Always enable skinning in materials**:
   ```javascript
   material.skinning = true;
   ```

2. **Update mixer in animation loop**:
   ```javascript
   function animate() {
     const delta = clock.getDelta();
     mixer.update(delta);
     // ...
   }
   ```

3. **Use `getSkeletonJointsFlat()` for performance**:
   - Flattened format is faster than hierarchical traversal
   - Better for large skeletons

4. **Normalize joint weights**:
   - USD data should already be normalized
   - But verify weights sum to 1.0 per vertex if issues occur

5. **Handle time conversion**:
   - USD often uses frame numbers
   - Three.js expects seconds
   - Use `fps` parameter: `time_seconds = frame_number / fps`

6. **Check skeleton compatibility**:
   ```javascript
   const mesh = usd.getMesh(meshId);
   if (mesh.skel_id >= 0) {
     // Mesh is skinned
     const skeleton = usd.getSkeleton(mesh.skel_id);
     // ...
   }
   ```

---

## Troubleshooting

### Animation not playing

- Check that `material.skinning = true`
- Verify mixer is being updated in animation loop
- Ensure animation targets the correct skeleton

### Mesh deformation incorrect

- Verify joint indices match between mesh and skeleton
- Check that bind pose matrices are correctly applied
- Ensure weights are normalized

### Performance issues

- Use `getSkeletonJointsFlat()` instead of `getSkeleton()`
- Reduce number of bones if possible
- Use lower FPS for animations
- Enable frustum culling

---

## API Reference

For complete Three.js documentation:
- [THREE.Skeleton](https://threejs.org/docs/#api/en/objects/Skeleton)
- [THREE.SkinnedMesh](https://threejs.org/docs/#api/en/objects/SkinnedMesh)
- [THREE.AnimationMixer](https://threejs.org/docs/#api/en/animation/AnimationMixer)
- [THREE.AnimationClip](https://threejs.org/docs/#api/en/animation/AnimationClip)

---

## License

Apache 2.0 - See LICENSE file for details.
