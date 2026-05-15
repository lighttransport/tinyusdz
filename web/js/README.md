# TinyUSDZ JS/WASM module development(for developer)

## Requrements

* npm or bun

## Setup

```
$ npm install
# or bun install
```

### Assets

Copy assets folder from demo directory by running `setup-assets.sh`

### Run

```
$ npm run dev
# or if you use bun
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

## Progress Callback (Three.js GLTFLoader Compatible)

TinyUSDZLoader supports progress callbacks compatible with Three.js GLTFLoader pattern. Progress is reported during download, parsing, and scene building phases.

### Basic Usage (GLTFLoader style)

```javascript
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

const loader = new TinyUSDZLoader();
await loader.init();

// Standard Three.js loader pattern
loader.load(
    'model.usdz',
    // onLoad
    (usd) => {
        console.log('USD loaded:', usd);
    },
    // onProgress - receives GLTFLoader-compatible event
    (event) => {
        console.log(`${event.stage}: ${event.percentage.toFixed(1)}% - ${event.message}`);
        // event.loaded - bytes loaded (during download) or normalized progress
        // event.total - total bytes or 1
        // event.stage - 'downloading' | 'parsing' | 'complete'
        // event.percentage - 0-100
        // event.message - human-readable status
    },
    // onError
    (error) => {
        console.error('Load failed:', error);
    }
);
```

### Full Progress with Scene Building

For complete progress reporting including Three.js scene building:

```javascript
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

const loader = new TinyUSDZLoader();
await loader.init();

loader.loadWithFullProgress(
    'model.usdz',
    // onLoad
    (result) => {
        console.log('USD object:', result.usd);
        if (result.scene) {
            scene.add(result.scene);
        }
    },
    // onProgress - unified progress across all phases
    (event) => {
        progressBar.style.width = `${event.percentage}%`;
        statusText.textContent = event.message;
        // Stages: 'downloading' (0-50%) | 'parsing' (50-80%) | 'building' (80-100%) | 'complete'
    },
    // onError
    (error) => {
        console.error('Load failed:', error);
    },
    // options
    {
        buildScene: true,
        sceneBuilder: TinyUSDZLoaderUtils.buildThreeNode.bind(TinyUSDZLoaderUtils),
        sceneBuilderOptions: {
            envMap: myEnvironmentMap,
            envMapIntensity: 1.0
        }
    }
);
```

### Scene Building Progress

When building Three.js scene graphs separately:

```javascript
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

// Build with progress callback
const scene = await TinyUSDZLoaderUtils.buildThreeNode(
    usd.getNode(0),  // root node
    null,            // default material
    usd,             // USD scene
    {
        onProgress: (info) => {
            console.log(`${info.percentage.toFixed(1)}% - ${info.message}`);
            // info.stage - 'building'
            // info.percentage - 0-100
            // info.message - e.g., "Building: MeshName (5/20)"
        },
        envMap: myEnvMap
    }
);
```

### Progress Event Object

| Field | Type | Description |
|-------|------|-------------|
| `loaded` | number | Bytes loaded (download) or normalized progress (0-1) |
| `total` | number | Total bytes or 1 |
| `stage` | string | Current stage: `'downloading'`, `'parsing'`, `'building'`, `'complete'` |
| `percentage` | number | Progress as percentage (0-100) |
| `message` | string | Human-readable status message |

### Async API

```javascript
// Promise-based loading with progress
const result = await loader.loadWithFullProgressAsync(url, onProgress, options);
```

## Material Conversion

TinyUSDZ supports both UsdPreviewSurface and OpenPBR (MaterialX) materials, with conversion to Three.js `MeshPhysicalMaterial`.

For detailed MaterialX documentation (OpenPBR parameter mappings, color space support, Blender export mapping, NodeGraph traversal, etc.), see [doc/materialx.md](../../doc/materialx.md).

### Quick Start

```javascript
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

const materialData = usdScene.getMaterial(materialId, 'json');

// Smart conversion (auto-selects best material type)
const material = await TinyUSDZLoaderUtils.convertMaterial(materialData, usdScene, {
    preferredMaterialType: 'auto',  // 'auto' | 'openpbr' | 'usdpreviewsurface'
    envMap: myEnvironmentMap,
    envMapIntensity: 1.0
});
```

## UsdLux Light Support

TinyUSDZ supports USD lighting (UsdLux) with conversion to Three.js lights (SphereLight, DistantLight, RectLight, DiskLight, CylinderLight, DomeLight). See the `usdlux.html` demo and `cli/dump-usdlux-cli.js` for usage examples.

## Demo Pages

The following demo pages are available:

| Demo | File | Description |
|------|------|-------------|
| **Basic Viewer** | `index.html` | Basic USD viewer with main.js |
| **MaterialX Demo** | `materialx.html` | Simple MaterialX/OpenPBR viewer with drag-and-drop USD loading, material parameter UI, and preset environment lighting |
| **MaterialX Debug Demo** | `mtlx-debug.html` | Advanced debugging demo with AOV visualization, node graph viewer, texture inspector, and comprehensive PBR debugging tools |
| **MaterialX WebGL2** | `materialx-webgl2.html` | Blender MaterialX with WebGL2 node graph |
| **MaterialX WebGPU** | `materialx-webgpu.html` | MaterialX with WebGPU + TSL |
| **MaterialX WebGPU (raw)** | `mtlx-webgpu.html` | Standalone WebGPU MaterialX |
| **Animation Demo** | `animation.html` | USD animation playback demo |
| **Animation Clips** | `anim-clips.html` | Per-object animation clip mixing |
| **Skinning Demo** | `skin-anim.html` | Skeletal animation and skinning demo |
| **UsdLux Demo** | `usdlux.html` | USD Lighting demo |
| **Subdivision Demo** | `subdiv.html` | Interactive Catmull-Clark, Loop, and Bilinear subdivision demo |
| **OffscreenGL** | `offscreengl.html` | WebWorker + OffscreenCanvas rendering |
| **Progress Demo** | `progress-demo.html` | Loading progress visualization |
| **Progress OffscreenGL** | `progress-offscreenwebgl.html` | OffscreenCanvas with progress + OOM recovery |
| **OpenPBR NodeGraph** | `openpbr-nodegraph-demo.html` | LiteGraph node graph editor |
| **MtlX Node Tester** | `mtlx-node-tester.html` | MaterialX node graph compile & run tester |

### Running Demos

```bash
# Start development server
npm run dev        # or: bun run dev

# Open a specific demo directly
npm run dev:mtlx               # materialx.html
npm run dev:anim               # animation.html
npm run dev:skel               # skin-anim.html
npm run dev:clips              # anim-clips.html
npm run dev:lux                # usdlux.html
npm run dev:subdiv             # subdiv.html
npm run dev:nodegraph          # openpbr-nodegraph-demo.html
npm run dev:offscreengl        # offscreengl.html
npm run dev:progress           # progress-demo.html
npm run dev:progress-offscreen # progress-offscreenwebgl.html
```

Experimental (WebGPU/WebGL2):

```bash
npm run dev:webgpu       # materialx-webgpu.html
npm run dev:webgpu-raw   # mtlx-webgpu.html
npm run dev:webgl2       # materialx-webgl2.html
```

## NPM packaging

NPM packaing is not handled in this folder.

Please see `../npm`

