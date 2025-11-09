# Three.js Animation System Documentation

## Overview

This document describes Three.js's keyframe animation system with a focus on rigid node animations (translation, scale, rotation/quaternion), particularly as it relates to glTF loading and the design considerations for Tydra RenderScene conversion.

## Core Animation Architecture

### 1. Animation System Hierarchy

The Three.js animation system consists of three main components:

```
Keyframes (raw data)
    ↓
KeyframeTrack (property animation)
    ↓
AnimationClip (collection of tracks)
    ↓
AnimationMixer (playback control)
```

### 2. KeyframeTrack Types

Three.js provides specialized track types for different properties:

- **VectorKeyframeTrack**: For `position` and `scale` (3D vectors)
- **QuaternionKeyframeTrack**: For `rotation` (quaternions)
- **NumberKeyframeTrack**: For single scalar values or individual components
- **ColorKeyframeTrack**: For color animations
- **BooleanKeyframeTrack**: For boolean properties
- **StringKeyframeTrack**: For string properties

## Rigid Node Animation Properties

### Translation (Position)

**Track Type**: `VectorKeyframeTrack`
**Property Path**: `.position`
**Value Type**: 3D vector [x, y, z]
**Units**: Scene units (typically meters in glTF)

```javascript
const positionKF = new THREE.VectorKeyframeTrack(
    '.position',
    [0, 1, 2],  // Times in seconds
    [0, 0, 0,   // Position at t=0: (x=0, y=0, z=0)
     30, 0, 0,  // Position at t=1: (x=30, y=0, z=0)
     0, 0, 0]   // Position at t=2: (x=0, y=0, z=0)
);
```

### Scale

**Track Type**: `VectorKeyframeTrack`
**Property Path**: `.scale`
**Value Type**: 3D vector [x, y, z]
**Units**: Scale factors (1.0 = no scaling)

```javascript
const scaleKF = new THREE.VectorKeyframeTrack(
    '.scale',
    [0, 1, 2],  // Times in seconds
    [1, 1, 1,   // Scale at t=0
     2, 2, 2,   // Scale at t=1 (2x in all axes)
     1, 1, 1]   // Scale at t=2
);
```

### Rotation

#### Quaternion Rotation (Recommended)

**Track Type**: `QuaternionKeyframeTrack`
**Property Path**: `.quaternion`
**Value Type**: Quaternion [x, y, z, w]
**Units**: Unit quaternion (normalized)

```javascript
const quaternionKF = new THREE.QuaternionKeyframeTrack(
    '.quaternion',
    [0, 1, 2],  // Times in seconds
    [0, 0, 0, 1,     // Identity quaternion at t=0
     0, 0.707, 0, 0.707,  // 90° Y rotation at t=1
     0, 0, 0, 1]     // Identity at t=2
);
```

**Important**: Three.js animations use quaternions for rotations, NOT Euler angles. This avoids gimbal lock and provides smooth interpolation via spherical linear interpolation (slerp).

#### Euler Angle Rotation (Limited Support)

While `Object3D.rotation` stores Euler angles, **animations cannot directly target Euler angles**. You must either:

1. Use quaternions (recommended)
2. Animate individual rotation components:

```javascript
// Animate Y-axis rotation only
const rotationYKF = new THREE.NumberKeyframeTrack(
    '.rotation[y]',  // Note the bracket notation
    [0, 1, 2],
    [0, Math.PI/2, 0]  // Radians
);
```

## Euler Angle Specifications

### Units
**All angles in Three.js are in RADIANS**, not degrees.

### Rotation Order
- **Default**: `'XYZ'`
- **Available orders**: `'XYZ'`, `'YZX'`, `'ZXY'`, `'XZY'`, `'YXZ'`, `'ZYX'`
- **Type**: Intrinsic Tait-Bryan angles
- **Meaning**: Rotations are performed with respect to the local coordinate system
  - For 'XYZ': Rotate around local X, then local Y, then local Z

### Setting Euler Order
```javascript
object.rotation.order = 'YXZ'; // Change rotation order
```

## glTF Animation Mapping

### glTF to Three.js Translation

When GLTFLoader loads animations, it maps:

| glTF Path | Three.js Track Type | Three.js Property |
|-----------|-------------------|-------------------|
| `translation` | VectorKeyframeTrack | `.position` |
| `rotation` | QuaternionKeyframeTrack | `.quaternion` |
| `scale` | VectorKeyframeTrack | `.scale` |
| `weights` | NumberKeyframeTrack | `.morphTargetInfluences[i]` |

### glTF Animation Structure

```json
{
  "animations": [{
    "channels": [{
      "sampler": 0,
      "target": {
        "node": 0,
        "path": "rotation"  // or "translation", "scale"
      }
    }],
    "samplers": [{
      "input": 0,     // Accessor for time values
      "output": 1,    // Accessor for property values
      "interpolation": "LINEAR"  // or "STEP", "CUBICSPLINE"
    }]
  }]
}
```

### Interpolation Modes

1. **STEP**: Discrete, no interpolation
   - Three.js: `THREE.InterpolateDiscrete`

2. **LINEAR**: Linear interpolation
   - Position/Scale: Linear interpolation
   - Rotation: Spherical linear interpolation (slerp)
   - Three.js: `THREE.InterpolateLinear`

3. **CUBICSPLINE**: Cubic spline with tangents
   - Requires in-tangent, value, out-tangent for each keyframe
   - Three.js: Custom interpolant in GLTFLoader
   - Can cause issues; "Always Sample Animation" in Blender forces LINEAR

### Loading and Playing glTF Animations

```javascript
const loader = new THREE.GLTFLoader();
loader.load('model.gltf', (gltf) => {
    scene.add(gltf.scene);

    // Create AnimationMixer
    const mixer = new THREE.AnimationMixer(gltf.scene);

    // Play all animations
    gltf.animations.forEach((clip) => {
        mixer.clipAction(clip).play();
    });

    // Update in render loop
    function animate(deltaTime) {
        mixer.update(deltaTime);
    }
});
```

## Property Path Syntax

Three.js supports complex property paths:

```javascript
'.position'              // Object position
'.rotation[x]'          // X component of rotation
'.scale'                // Object scale
'.material.opacity'     // Material opacity
'.bones[R_hand].scale'  // Specific bone scale
'.materials[3].diffuse[r]'  // Red channel of 4th material
'.morphTargetInfluences[0]'  // First morph target
```

## Data Storage Format

Keyframe data is stored in flat arrays:

```javascript
// For VectorKeyframeTrack (3 values per keyframe)
times = [0, 1, 2];
values = [x0, y0, z0,  x1, y1, z1,  x2, y2, z2];

// For QuaternionKeyframeTrack (4 values per keyframe)
times = [0, 1, 2];
values = [x0, y0, z0, w0,  x1, y1, z1, w1,  x2, y2, z2, w2];
```

## Complete Animation Example

```javascript
// Create keyframe tracks
const positionKF = new THREE.VectorKeyframeTrack(
    '.position',
    [0, 1, 2],
    [0, 0, 0,  10, 5, 0,  0, 0, 0]
);

const quaternionKF = new THREE.QuaternionKeyframeTrack(
    '.quaternion',
    [0, 1, 2],
    [0, 0, 0, 1,
     0, 0.707, 0, 0.707,
     0, 0, 0, 1]
);

const scaleKF = new THREE.VectorKeyframeTrack(
    '.scale',
    [0, 1, 2],
    [1, 1, 1,  2, 2, 2,  1, 1, 1]
);

// Create animation clip
const clip = new THREE.AnimationClip('Action', 2, [
    positionKF,
    quaternionKF,
    scaleKF
]);

// Create mixer and play
const mixer = new THREE.AnimationMixer(mesh);
const action = mixer.clipAction(clip);
action.play();

// Update in render loop
function animate() {
    const delta = clock.getDelta();
    mixer.update(delta);
    renderer.render(scene, camera);
    requestAnimationFrame(animate);
}
```

## Design Considerations for Tydra RenderScene

### Key Points for Implementation

1. **Use Quaternions for Rotations**
   - Store rotations as quaternions to match Three.js animation system
   - Avoid Euler angles in animation data

2. **Radians for All Angles**
   - Ensure all angle values are in radians
   - Convert from degrees if necessary

3. **Flat Array Storage**
   - Store keyframe values in flat arrays
   - Values array length = times array length × components per value

4. **Support Multiple Interpolation Modes**
   - Implement STEP, LINEAR, and optionally CUBICSPLINE
   - Use slerp for quaternion interpolation

5. **Time in Seconds**
   - Animation times should be in seconds (floating point)
   - Support arbitrary time ranges

6. **Property Path Mapping**
   - Map USD properties to Three.js property paths
   - Support nested property access

### Recommended Data Structure

```cpp
struct AnimationChannel {
    enum PropertyType {
        TRANSLATION,  // vec3
        ROTATION,     // quat
        SCALE        // vec3
    };

    PropertyType type;
    std::vector<float> times;     // Keyframe times in seconds
    std::vector<float> values;    // Flat array of values
    InterpolationType interpolation;  // STEP, LINEAR, CUBICSPLINE
    int nodeIndex;                // Target node
};

struct AnimationClip {
    std::string name;
    float duration;
    std::vector<AnimationChannel> channels;
};
```

## Known Issues and Workarounds

1. **Cubic Spline Issues**: GLTFLoader has had problems with cubic spline interpolation. Consider using LINEAR interpolation or implementing custom handling.

2. **Rotation > 360°**: When exporting from Blender, rotations over 360° may not export correctly to glTF. Use quaternions to avoid this issue.

3. **Gimbal Lock**: Using Euler angles can cause gimbal lock. Always prefer quaternions for rotations.

4. **Performance**: Large numbers of keyframes can impact performance. Consider optimizing by reducing keyframe count where possible.

## References

- [Three.js Animation System Documentation](https://threejs.org/docs/#manual/en/introduction/Animation-system)
- [Three.js Euler Documentation](https://threejs.org/docs/#api/en/math/Euler)
- [Three.js Quaternion Documentation](https://threejs.org/docs/#api/en/math/Quaternion)
- [glTF 2.0 Animation Specification](https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#animations)
- [GLTFLoader Source Code](https://github.com/mrdoob/three.js/blob/master/examples/jsm/loaders/GLTFLoader.js)