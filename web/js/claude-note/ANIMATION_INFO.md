# USD Animation Information Tool

This directory contains tools to load USD/USDA/USDC/USDZ files and extract animation information using the TinyUSDZ WASM module.

## Overview

The animation information tool provides:
- USD file loading and parsing
- Animation clip detection and information display
- Scene information summary
- Memory usage tracking
- Detailed animation track analysis (optional)

## Scripts

### `animation-info.js`
The main Node.js script that loads USD files and prints animation information.

**Features:**
- Supports `.usd`, `.usda`, `.usdc`, and `.usdz` files
- Displays all animation clips in a USD file
- Shows clip metadata (name, duration, channel count, sampler count)
- Optional detailed animation track information
- Memory usage reporting
- Error handling and helpful error messages

**Usage:**
```bash
# Using vite-node (recommended)
npm run anim-info <path-to-usd-file>
npm run anim-info:detailed <path-to-usd-file>

# Or directly
vite-node animation-info.js <path-to-usd-file> [options]
node animation-info.js <path-to-usd-file> [options]

# With shell wrapper
./animation-info.sh <path-to-usd-file> [options]
```

**Options:**
- `--detailed` - Print detailed animation track information
- `--memory` - Print memory usage statistics at the end
- `--help` - Show help message

**Examples:**
```bash
# Load a USDC file and show animation info
vite-node animation-info.js ../../models/suzanne-subd-lv4.usdc

# Show detailed animation data
vite-node animation-info.js animation.usd --detailed

# Show memory usage
vite-node animation-info.js model.usdz --memory

# Show both detailed info and memory
vite-node animation-info.js model.usdz --detailed --memory

# Show help
vite-node animation-info.js --help
```

**Output Example:**
```
Loading: ../../models/suzanne-subd-lv4.usdc (2.34 MB)

✓ USD file loaded successfully (1234ms)

=== Scene Information ===

=== Animation Information ===
Total animation clips: 2

--- Animation Clip 0 ---
  Name: Armature|ArmatureAction
  Prim Name: Armature
  Absolute Path: /Root/Armature/ArmatureAction
  Duration: 2.5s
  Channels: 24
  Samplers: 24
  Animation Type: Skeletal

--- Animation Clip 1 ---
  Name: CubeAnimation
  Prim Name: Cube
  Absolute Path: /Root/Cube
  Duration: 3.0s
  Channels: 3
  Samplers: 3
  Animation Type: Node Transform
```

**Detailed Output Example** (with `--detailed`):
```
--- Animation Clip 0 ---
  Name: Armature|ArmatureAction
  ...
  Animation Type: Skeletal

  Channel Details:
    Channel 0:
      Target Type: SkeletonJoint
      Path: Translation
      Skeleton ID: 0
      Joint ID: 0
      Sampler Index: 0
      Keyframes: 48
      Time Range: 0.000s - 2.500s
      Interpolation: Linear
    Channel 1:
      Target Type: SkeletonJoint
      Path: Rotation
      Skeleton ID: 0
      Joint ID: 0
      ...

  Channel Summary:
    Node Transform Channels: 0
    Skeletal Joint Channels: 24

--- Animation Clip 1 ---
  Name: CubeAnimation
  ...
  Animation Type: Node Transform

  Channel Details:
    Channel 0:
      Target Type: SceneNode
      Path: Translation
      Target Node: 5
      Sampler Index: 0
      Keyframes: 60
      Time Range: 0.000s - 3.000s
    ...

  Channel Summary:
    Node Transform Channels: 3
    Skeletal Joint Channels: 0
```

## Integration with WASM Module

### Module Loading

The scripts use ES6 module imports to load the TinyUSDZ WASM module:

```javascript
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
```

The WASM module files are located in `src/tinyusdz/`:
- `tinyusdz.js` - 32-bit WASM loader
- `tinyusdz.wasm` - 32-bit WASM binary
- `tinyusdz_64.js` - 64-bit WASM loader (if built)
- `tinyusdz_64.wasm` - 64-bit WASM binary (if built)

### Building WASM Modules

The WASM modules are built from C++ source using Emscripten. They must be compiled separately:

**32-bit build:**
```bash
cd web
./bootstrap-linux.sh
cd build
make
```

**64-bit build:**
```bash
cd web
./bootstrap-linux-wasm64.sh
cd build_64
make
```

The compiled output is copied to `js/src/tinyusdz/` by the CMake build process.

### Module Update Workflow

If you modify C++ source files and need to update the WASM modules:

1. **Modify C++ source** in `src/` directory
2. **Rebuild WASM modules:**
   ```bash
   cd web
   ./bootstrap-linux.sh          # For 32-bit
   ./bootstrap-linux-wasm64.sh   # For 64-bit
   cd build && make
   cd ../build_64 && make
   ```
3. **Verify modules updated:**
   ```bash
   ls -la js/src/tinyusdz/
   ```
4. **Test with scripts:**
   ```bash
   npm run anim-info <test-file>
   ```

## API Reference

### TinyUSDZLoader

Main loader class for USD files:

```javascript
const loader = new TinyUSDZLoader();
await loader.init({ useMemory64: false });
loader.setMaxMemoryLimitMB(512);

// Load file
loader.load(url, onLoad, onProgress, onError);
```

### Animation Methods

On the loaded USD object:

```javascript
// Get number of animation clips
const numClips = usd.numAnimations();

// Get animation info (metadata)
const info = usd.getAnimationInfo(clipIndex);
// Returns: {
//   name,
//   prim_name,
//   abs_path,
//   display_name,
//   duration,
//   num_channels,
//   num_samplers,
//   has_skeletal_animation,  // NEW: true if contains joint animations
//   has_node_animation       // NEW: true if contains node transform animations
// }

// Get full animation data with channels and samplers
const anim = usd.getAnimation(clipIndex);
// Returns: {
//   name,
//   prim_name,
//   abs_path,
//   display_name,
//   duration,
//   channels: [
//     {
//       target_type,    // NEW: 'SceneNode' or 'SkeletonJoint'
//       path,           // 'Translation', 'Rotation', 'Scale', or 'Weights'
//       target_node,    // For SceneNode: index into scene nodes
//       skeleton_id,    // NEW: For SkeletonJoint: skeleton index
//       joint_id,       // NEW: For SkeletonJoint: joint index
//       sampler         // Index into samplers array
//     }
//   ],
//   samplers: [
//     {
//       times: [0.0, 0.5, 1.0, ...],
//       values: [...],      // Flat array of keyframe values
//       interpolation       // 'Linear', 'Step', or 'CubicSpline'
//     }
//   ]
// }

// Get all animations at once
const allAnims = usd.getAllAnimations();
const allInfos = usd.getAllAnimationInfos();
```

### Animation Channel Types

TinyUSDZ now distinguishes between two types of animation channels:

**SceneNode Animations** (from USD xformOps):
- Animate transform properties of scene nodes
- Use `target_node` to identify which node
- Examples: Camera movement, object position/rotation, prop animations

**SkeletonJoint Animations** (from USD SkelAnimation):
- Animate skeleton joint transforms
- Use `skeleton_id` + `joint_id` to identify which joint
- Examples: Character rigs, facial animation, skinned meshes

This separation matches glTF 2.0 animation model and enables proper handling of both animation types in rendering engines like Three.js.

## Node.js Requirements

- Node.js v24.0 or later (for WASM support)
- Compatible ES6 module support

## Troubleshooting

### "Native module not initialized"
- Ensure WASM modules are built: check `js/src/tinyusdz/` directory
- Verify the build completed successfully without errors
- Rebuild if necessary: `cd web && ./bootstrap-linux.sh && cd build && make`

### "Failed to load USD from binary"
- File may be corrupted or invalid USD format
- Check file size and format
- Try with a known-good USD file first

### "File not found"
- Check file path is correct and file exists
- Use absolute paths or paths relative to where you run the script
- Ensure the file has proper read permissions

### Memory issues on large files
- Increase memory limit: `loader.setMaxMemoryLimitMB(2048);`
- Use 64-bit WASM: `await loader.init({ useMemory64: true });`
- Process files in smaller chunks if possible

## Performance Tips

1. **Use 32-bit for most cases** - lower memory overhead
2. **Switch to 64-bit for large files** - can use up to 8GB memory
3. **Set appropriate memory limits** - prevents excessive heap growth
4. **Stream large files** - if supported by your use case
5. **Cache loaded modules** - reuse TinyUSDZLoader instance

## Development Notes

### Building from Source

See the main CLAUDE.md for complete build instructions. Key points:

1. **Emscripten is required:**
   ```bash
   source /path/to/emsdk/emsdk_env.sh
   ```

2. **Configure build:**
   ```bash
   cd web
   emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -Bbuild
   ```

3. **Build:**
   ```bash
   cd build
   make
   ```

### Adding New Features

If you need to add new functionality:

1. **Modify binding.cc** - Add new bindings for C++ functions
2. **Update TinyUSDZLoader.js** - Add new JS wrapper methods if needed
3. **Rebuild WASM modules** - Follow the build workflow above
4. **Test with animation-info.js** - Verify functionality

### Known Limitations

- WASM modules load asynchronously (no synchronous loading)
- File I/O must use Blob/File API or fs module in Node.js
- Memory limits are enforced to prevent runaway allocations
- Some USD features may have limited support in WASM build

## See Also

- Main project: `../../CLAUDE.md`
- WASM binding code: `../binding.cc`
- TinyUSDZ source: `../../src/`
- Build scripts: `../bootstrap-*.sh`

