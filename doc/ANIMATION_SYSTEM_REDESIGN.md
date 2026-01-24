# Animation System Redesign for Three.js Compatibility

## Overview

The Tydra RenderScene animation system has been redesigned to be compatible with Three.js/glTF animation architecture. The new design separates animation data from the node hierarchy, making it easier to export to and work with Three.js/WebGL applications.

## Key Changes

### 1. New Animation Structures

#### AnimationInterpolation (enum)
Matches glTF interpolation modes:
- `Linear` - Linear interpolation (slerp for quaternions)
- `Step` - Discrete/stepped interpolation
- `CubicSpline` - Cubic spline with tangents

#### AnimationPath (enum)
Defines which property is animated (matches glTF animation paths):
- `Translation` - Position (vec3) → Three.js `.position`
- `Rotation` - Rotation (quaternion) → Three.js `.quaternion`
- `Scale` - Scale (vec3) → Three.js `.scale`
- `Weights` - Morph target weights (float array) → Three.js `.morphTargetInfluences`

#### KeyframeSampler (struct)
Stores keyframe data in flat arrays (matches glTF sampler and Three.js KeyframeTrack):
```cpp
struct KeyframeSampler {
  std::vector<float> times;   // Keyframe times in seconds
  std::vector<float> values;  // Flat array of values
  AnimationInterpolation interpolation;
};
```

**Value format:**
- Translation/Scale: `[x0,y0,z0, x1,y1,z1, ...]` (3 floats/keyframe)
- Rotation: `[x0,y0,z0,w0, x1,y1,z1,w1, ...]` (4 floats/keyframe)
- Weights: `[w0, w1, w2, ...]` (1 float/keyframe/target)

#### AnimationChannel (struct)
Binds sampler to a node property (matches glTF animation channel):
```cpp
struct AnimationChannel {
  AnimationPath path;       // Which property to animate
  int32_t target_node;      // Index into RenderScene::nodes
  int32_t sampler;          // Index into AnimationClip::samplers
};
```

#### AnimationClip (struct)
Collection of channels and samplers (matches glTF Animation and Three.js AnimationClip):
```cpp
struct AnimationClip {
  std::string name;
  std::string prim_name;
  std::string abs_path;
  std::string display_name;
  float duration;
  std::vector<KeyframeSampler> samplers;
  std::vector<AnimationChannel> channels;
};
```

### 2. Removed from Node

The `node_animations` field has been **removed** from the `Node` struct. Animation data is now managed independently at the `RenderScene` level.

**Before:**
```cpp
struct Node {
  // ...
  std::vector<AnimationChannel> node_animations;  // OLD: Embedded in node
};
```

**After:**
```cpp
struct Node {
  // ...
  // Animation data moved to RenderScene::animations
  // Animations reference nodes by index (AnimationChannel::target_node)
};
```

### 3. RenderScene Integration

`RenderScene::animations` now uses `AnimationClip`:
```cpp
class RenderScene {
  // ...
  std::vector<AnimationClip> animations;  // NEW: AnimationClip instead of Animation
};
```

## Migration Path

### Old System (USD-centric)
```cpp
// Animation embedded in each node
node.node_animations[i].type = AnimationChannel::ChannelType::Translation;
node.node_animations[i].translations.samples[j].t = time;
node.node_animations[i].translations.samples[j].value = position;
```

### New System (glTF/Three.js compatible)
```cpp
// Animation as independent clips
AnimationClip clip;
clip.name = "Walk";

// Create sampler
KeyframeSampler sampler;
sampler.times = {0.0f, 1.0f, 2.0f};
sampler.values = {0,0,0,  1,0,0,  0,0,0};  // Flat array
sampler.interpolation = AnimationInterpolation::Linear;
clip.samplers.push_back(sampler);

// Create channel linking sampler to node property
AnimationChannel channel;
channel.path = AnimationPath::Translation;
channel.target_node = 5;  // Index into RenderScene::nodes
channel.sampler = 0;       // Index into clip.samplers
clip.channels.push_back(channel);

// Add to scene
scene.animations.push_back(clip);
```

## Benefits

1. **Three.js Compatibility**: Direct mapping to Three.js AnimationClip/KeyframeTrack
2. **glTF Compatible**: Matches glTF 2.0 animation structure
3. **Separation of Concerns**: Animations independent from scene hierarchy
4. **Memory Efficient**: Flat array storage reduces overhead
5. **Flexible**: Multiple channels can target the same node with different samplers

## Three.js Export Example

```javascript
// In Three.js/JavaScript
const times = new Float32Array(samplers[0].times);
const values = new Float32Array(samplers[0].values);

// Create KeyframeTrack based on path
let track;
switch(channel.path) {
  case 'Translation':
    track = new THREE.VectorKeyframeTrack(
      '.position', times, values
    );
    break;
  case 'Rotation':
    track = new THREE.QuaternionKeyframeTrack(
      '.quaternion', times, values
    );
    break;
  case 'Scale':
    track = new THREE.VectorKeyframeTrack(
      '.scale', times, values
    );
    break;
}

// Create AnimationClip
const clip = new THREE.AnimationClip(name, duration, [track]);
```

## Important Notes

1. **Rotations use Quaternions**: Not Euler angles (matches Three.js requirement)
2. **Times in Seconds**: All animation times are in seconds (float)
3. **Flat Arrays**: Values stored as flat float arrays for efficiency
4. **Node Indexing**: Channels reference nodes by index, not path
5. **Backward Compatibility**: Old `AnimationSampler<T>` template still exists for legacy USD data

## Related Documentation

- See `doc/THREEJS_ANIMATION.md` for Three.js animation system details
- See glTF 2.0 specification for animation format details

## Implementation Status

- ✅ Header structures defined (`src/tydra/render-data.hh`)
- ✅ `node_animations` removed from `Node`
- ✅ `RenderScene::animations` updated to use `AnimationClip`
- ✅ Function signatures updated
- ✅ Compilation verified
- ⏳ Implementation of converter functions (TODO)
- ⏳ Three.js exporter (TODO)
