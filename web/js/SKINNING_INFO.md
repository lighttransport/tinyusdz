# Skinning Info CLI Tool

A Node.js command-line tool for inspecting skinning information in USD files.

## Usage

```bash
npx vite-node skinning-info.js <path-to-usd-file> [options]
```

### Options

- `--detailed` - Print detailed skinning and animation information
- `--keyframes` - Dump skeletal animation keyframe data
- `--memory` - Print memory usage statistics
- `--reduce-bones` - Enable bone reduction during USD loading
- `--target-bones <N>` - Set target bone count per vertex (default: 4, requires `--reduce-bones`)
- `--help` - Show help message

### Examples

```bash
# Basic usage
npx vite-node skinning-info.js ../../models/character.usdc

# Detailed skinning information
npx vite-node skinning-info.js skinned-mesh.usd --detailed

# Show skeletal animation keyframes
npx vite-node skinning-info.js character.usda --detailed --keyframes

# Include memory usage stats
npx vite-node skinning-info.js model.usdz --detailed --memory

# Load with bone reduction enabled (reduce to 4 bones per vertex)
npx vite-node skinning-info.js character.usdc --reduce-bones --detailed

# Load with custom target bone count (reduce to 2 bones per vertex)
npx vite-node skinning-info.js character.usdc --reduce-bones --target-bones 2

# Combine bone reduction with detailed output and keyframes
npx vite-node skinning-info.js model.usda --reduce-bones --target-bones 3 --detailed --keyframes
```

## What It Displays

This tool is designed to show:

1. **Mesh Skinning Data**
   - Joint indices and joint weights per vertex
   - Number of influences per vertex
   - Unique joints used by each mesh
   - Geometry bind transform matrices
   - Weight statistics (min, max, average)
   - Sample skinning data for vertices

2. **Skeleton Hierarchy**
   - Skeleton structure and joint relationships
   - Bind pose and rest pose transforms
   - Joint names and indices

3. **Skeletal Animation**
   - Animation clips from SkelAnimation prims
   - Per-joint animation channels (translation, rotation, scale)
   - Keyframe times and values
   - Interpolation methods
   - Animation duration and time ranges

## Bone Reduction

The tool supports bone reduction settings that are applied during USD file loading. Bone reduction can optimize skinned meshes by reducing the number of bone influences per vertex, which can improve rendering performance.

### When to Use Bone Reduction

- **Performance Optimization**: Reduce from 8+ influences to 4 or fewer for real-time rendering
- **Hardware Constraints**: Target specific GPU limitations (e.g., mobile devices)
- **Quality vs Performance**: Balance visual quality against rendering cost

### How It Works

When `--reduce-bones` is enabled:

1. The loader processes each skinned mesh during loading
2. For each vertex, it keeps only the N strongest bone influences
3. Weights are automatically renormalized to sum to 1.0
4. The reduced data is what gets exposed through the JavaScript API

### Example Workflows

```bash
# Check if a model has excessive bone influences
npx vite-node skinning-info.js character.usdc --detailed

# Load with reduction to see the effect on bone count
npx vite-node skinning-info.js character.usdc --reduce-bones --target-bones 4 --detailed

# Compare before/after by running without and with reduction
npx vite-node skinning-info.js model.usda --detailed > before.txt
npx vite-node skinning-info.js model.usda --reduce-bones --target-bones 2 --detailed > after.txt
```

### Technical Notes

- Bone reduction happens at load time in the native C++ layer
- The reduction uses a greedy algorithm (keeps strongest weights)
- Original data in the USD file is not modified
- Reduction only affects meshes with skinning data

## Current Status

✅ **Fully Functional**: The JavaScript/WebAssembly binding now exposes all skinning data from the C++ layer!

The following fields from `RenderMesh::joint_and_weights` are now available through the `getMesh()` function:

- `jointIndices` - Int32Array of joint indices per vertex
- `jointWeights` - Float32Array of joint weights per vertex
- `elementSize` - Number of influences per vertex (integer)
- `geomBindTransform` - Float64Array with geometry bind transform matrix (4x4, 16 doubles)
- `skel_id` - Reference to skeleton ID (integer, -1 if not skinned)

### Implementation

The binding implementation in `web/binding.cc` exports skinning data using Emscripten's typed_memory_view for zero-copy access to the underlying C++ arrays. Bone reduction settings are applied during USD loading when `setEnableBoneReduction(true)` and `setTargetBoneCount(N)` are called on the loader.

## Related Tools

- `animation-info.js` - View general animation information (node transforms and skeletal)
- See `ANIMATION_INFO.md` for documentation on the animation info tool

## Technical Details

### Data Structures

The tool works with mesh objects that have the following structure:

```javascript
{
  // Mesh geometry
  primName: string,
  absPath: string,
  points: Float32Array,
  normals: Float32Array,
  faceVertexIndices: Uint32Array,
  faceVertexCounts: Uint32Array,

  // Skinning data
  jointIndices: Int32Array,       // Flat array: vertex0_joints..., vertex1_joints...
  jointWeights: Float32Array,     // Flat array: vertex0_weights..., vertex1_weights...
  elementSize: number,            // Influences per vertex (e.g., 4)
  skel_id: number,                // Index into RenderScene::skeletons (-1 if not skinned)
  geomBindTransform: Float64Array // 4x4 matrix as 16 doubles (row-major)
}
```

### Animation Data

Skeletal animations are identified by:
- `target_type === 'SkeletonJoint'` in animation channels
- Presence of `skeleton_id` and `joint_id` fields
- `has_skeletal_animation` flag in animation info

## See Also

- [TinyUSDZ Documentation](https://github.com/lighttransport/tinyusdz)
- [USD Skeleton Schema](https://graphics.pixar.com/usd/docs/api/usd_skel_page_front.html)
