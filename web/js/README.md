# TinyUSDZ JS/WASM module development(for developer)

## Requrements

* bun

## Setup

```
$ bun install
```

### Assets

Copy assets folder from demo directory by running `setup-assets.sh`

### Run

```
$ bun run dev
```

## Memory Limit Configuration

TinyUSDZLoader now supports memory limit configuration to prevent memory exhaustion attacks when loading potentially malicious USD files.

### Usage

```javascript
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

// Option 1: Set memory limit in constructor
// (Default: 2GB for WASM32, 8GB for WASM64)
const loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });

// Option 2: Set memory limit after creation
const loader2 = new TinyUSDZLoader();
loader2.setMaxMemoryLimitMB(1024); // Set 1GB limit

// Option 3: Override memory limit for specific load operations
loader.load(url, onLoad, onProgress, onError, { maxMemoryLimitMB: 256 });

// Option 4: Check the native default memory limit
const defaultLimit = await loader.getNativeDefaultMemoryLimitMB();
console.log(`Native default memory limit: ${defaultLimit} MB`);
```

### Security Considerations

- Default memory limits:
  - **2GB for 32-bit WASM** (standard build)
  - **8GB for 64-bit WASM** (MEMORY64 build)
- Memory limits are enforced at the WASM level during USD parsing
- Lower limits are recommended for untrusted USD files
- Memory limit applies to both Stage and Layer loading operations

## NPM packaging

NPM packaing is not handled in this folder.

Please see `../npm`

## Gaussian Splatting Demo

The `gsplat.html` and `gsplat.js` files provide a web-based demo for loading and visualizing Gaussian splat data.

### Supported Formats

The demo supports three file formats:

1. **USD Files** (.usd, .usda, .usdc, .usdz)
   - GeomPoints with `primvars:gsplat:*` attributes
   - Example attributes: scales, rotations, alphas, sh_l0, shDegree

2. **SPZ Files** (.spz)
   - Niantic SPZ compressed Gaussian splat format
   - ~10x smaller than PLY with minimal quality loss
   - Obtained from Scaniverse app or converted from PLY

3. **PLY Files** (.ply)
   - Standard PLY format with Gaussian splat vertex properties
   - Output from 3D Gaussian Splatting training
   - Binary format with gaussian-specific fields (scale_0-2, rot_0-3, opacity, f_dc_0-2, f_rest_*)

### Running the Demo

```bash
# Start development server
bun run dev

# Navigate to the gsplat demo
# Open http://localhost:5173/gsplat.html
```

Or use the dedicated npm script:

```bash
bun run dev:gsplat
```

### Loading Files

1. Click "📁 Load File" button
2. Select a .usd, .spz, or .ply file
3. The file will be processed by TinyUSDZ
4. Gaussian splats will render as a point cloud (temporary visualization)

**Note**: The current implementation renders splats as point clouds. For production use, integrate with a proper Gaussian splatting renderer like:
- @google/model-viewer with splat support
- antimatter15/splat
- three-gpu-pathtracer GSplat

### Example Files

For testing, you can use:
- USD files from `../../models/gsplat-reference-example.usda`
- SPZ files from Scaniverse app export
- PLY files from [3D Gaussian Splatting](https://github.com/graphdeco-inria/gaussian-splatting) training


