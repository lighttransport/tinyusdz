# Skeletal Animation Demo

This demo shows how to extract and play USD skeletal animations (SkelAnimation) using Three.js SkinnedMesh.

## Files

- **skining-anim.html** - HTML page for the skeletal animation demo
- **skining-anim.js** - JavaScript implementation with skeleton extraction and animation playback

## Features

### Skeletal Animation Support
- ✅ Extracts USD SkelAnimation data
- ✅ Builds Three.js Skeleton from USD skeleton hierarchy
- ✅ Creates SkinnedMesh with proper bone binding
- ✅ Filters for SkeletonJoint channels only (ignores node animations)
- ✅ Maps joint animations to bone transforms
- ✅ Supports Translation, Rotation, and Scale bone animations

### Visualization
- Real-time skeleton visualization with SkeletonHelper
- Toggle skeleton display on/off
- Interactive camera controls (orbit, pan, zoom)
- FPS counter
- Animation timeline scrubbing

### Animation Controls
- Play/Pause animation
- Reset to beginning
- Speed control (0x to 3x)
- Animation selection dropdown
- Timeline position display

## How It Works

### 1. Skeleton Extraction

The demo extracts USD skeleton hierarchy and converts it to Three.js bones:

```javascript
// USD SkelNode hierarchy -> Three.js Bone hierarchy
function buildSkeletonFromUSD(usdSkeleton, skeletonId) {
  // Recursively build bone hierarchy from USD SkelNode
  // Maps joint_id to THREE.Bone for animation targeting
  // Applies rest transforms to bones
}
```

### 2. Animation Conversion

Skeletal animations are converted from USD channels/samplers to Three.js KeyframeTracks:

```javascript
// Filter for SkeletonJoint channels
const skeletalChannels = channels.filter(ch =>
  ch.target_type === 'SkeletonJoint'
);

// Map channels by joint_id
// Create KeyframeTracks for each bone:
//   - Translation -> VectorKeyframeTrack for bone.position
//   - Rotation -> QuaternionKeyframeTrack for bone.quaternion
//   - Scale -> VectorKeyframeTrack for bone.scale
```

### 3. Playback

Animations are played using Three.js AnimationMixer:

```javascript
mixer = new THREE.AnimationMixer(skinnedMesh);
animationAction = mixer.clipAction(clip);
animationAction.play();

// In animation loop:
mixer.update(deltaTime);
```

## Usage

### Running the Demo

**Option 1: Using a development server**
```bash
# If you have a local server
cd web/js
python -m http.server 8000
# Open http://localhost:8000/skining-anim.html
```

**Option 2: Using vite**
```bash
cd web/js
npx vite
# Open the displayed URL and navigate to skining-anim.html
```

### Loading USD Files

1. Click "Load USD File" button
2. Select a USD file with skeletal animation (`.usd`, `.usda`, `.usdc`, `.usdz`)
3. The skeleton and animations will be extracted automatically
4. Animations will start playing automatically

### Supported USD Files

The demo requires USD files with:
- **Skeleton** (`skel:skeleton` relationship on mesh)
- **SkelAnimation** (animation source with `translations`, `rotations`, `scales`)
- **Skinning data** (joint indices and weights on mesh)

Example USD structure:
```
/Root
  /Character (Skeleton)
    skel:joints = ["/Root/Character/Hips", "/Root/Character/Spine", ...]
    skel:bindTransforms = [...]
    skel:restTransforms = [...]
    skel:animationSource = </Root/Character/Anim>
  /CharacterMesh (Mesh)
    skel:skeleton = </Root/Character>
    primvars:skel:jointIndices = [...]
    primvars:skel:jointWeights = [...]
  /Anim (SkelAnimation)
    joints = [...]
    translations.timeSamples = { 0: [...], 1: [...], ... }
    rotations.timeSamples = { 0: [...], 1: [...], ... }
    scales.timeSamples = { 0: [...], 1: [...], ... }
```

## Architecture Differences from animation.js

| Feature | animation.js | skining-anim.js |
|---------|-------------|-----------------|
| **Target** | SceneNode animations | SkeletonJoint animations |
| **Filter** | `target_type === 'SceneNode'` | `target_type === 'SkeletonJoint'` |
| **Mapping** | target_node → scene nodes | skeleton_id + joint_id → bones |
| **Three.js** | Animates Object3D transforms | Animates Bone transforms |
| **Structure** | Scene hierarchy | Skeleton hierarchy with SkinnedMesh |
| **Helper** | None | SkeletonHelper for visualization |

## Channel Structure

The new animation architecture uses:

```javascript
{
  target_type: 'SkeletonJoint',  // Identifies skeletal animation
  skeleton_id: 0,                 // Index into USD skeletons array
  joint_id: 5,                    // Index into skeleton's joints array
  path: 'Rotation',               // Translation/Rotation/Scale
  sampler: 2                      // Index into samplers array
}
```

## API Reference

### Key Functions

**buildSkeletonFromUSD(usdSkeleton, skeletonId)**
- Builds Three.js bone hierarchy from USD skeleton
- Returns: `{ bones: Bone[], boneMap: Map, rootBone: Bone }`

**convertUSDSkeletalAnimationsToThreeJS(usdLoader, boneMap)**
- Extracts skeletal animations and creates AnimationClips
- Filters for SkeletonJoint channels only
- Returns: `AnimationClip[]`

**playAnimation(index)**
- Plays animation by index
- Stops current animation and starts new one

### USD Loader Methods

```javascript
// Get skeleton count
const numSkeletons = usd_scene.numSkeletons();

// Get skeleton by index
const skeleton = usd_scene.getSkeleton(0);
// Returns: { root_node, prim_name, abs_path, display_name, anim_id }

// Get animations (same as animation.js)
const animations = usd_scene.getAllAnimations();
const animInfos = usd_scene.getAllAnimationInfos();
```

## Troubleshooting

### "No skeletons found in USD file"
- File doesn't contain a Skeleton prim
- Load a file with skeletal animation (character rigs, etc.)

### Skeleton appears but doesn't animate
- Check that SkelAnimation has time-sampled data
- Verify joint names match between Skeleton and SkelAnimation
- Check console for animation extraction errors

### Animation plays but mesh doesn't deform
- Mesh may not have proper skinning data (joint indices/weights)
- Verify `skel:skeleton` relationship points to correct Skeleton
- Check that joint influences are properly authored

### Bones in wrong positions
- USD may use different coordinate system
- Check rest transforms vs bind transforms
- Try toggling skeleton visualization to debug

## Performance Tips

1. **Skeleton complexity**: More bones = more computation
   - Typical character: 50-100 bones
   - High-detail rig: 200+ bones

2. **Animation data**: Large keyframe counts increase memory
   - Consider resampling animations to lower frame rates
   - Remove unnecessary channels

3. **Multiple characters**: Each SkinnedMesh needs its own mixer
   - Create separate AnimationMixer per character
   - Reuse AnimationClip across multiple characters

## Examples

### Expected Console Output

```
USD scene loaded: { ... }
Found 1 skeletons in USD file
USD Skeleton: { root_node: {...}, prim_name: "Armature", ... }
Built skeleton with 24 bones
Found skinned mesh: CharacterMesh
Found 1 animations in USD file
Processing animation 0: Walk
Animation 0: 24 skeletal channels (0 node channels skipped)
Created skeletal clip: Walk, duration: 2.5s, tracks: 72
Extracted 1 skeletal animations from USD file
Animation 0: Walk, duration: 2.5s, tracks: 72 [skeletal]
Playing animation: Walk
```

### UI Display

```
Skeleton Information:
  Skeletons: 1
  Total Joints: 24

Skeletal Animations Found:
  0: Walk - 2.50s, 72 tracks [Skeletal]
```

## See Also

- **animation.js** - Node transform animation demo
- **ANIMATION_INFO.md** - Animation extraction documentation
- **TinyUSDZ docs** - USD file format specifications
