# LightUSD WebAssembly Build

This directory contains the WebAssembly build configuration for LightUSD.

## Prerequisites

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (3.1.0 or later)
- CMake 3.16+
- Make or Ninja

## Quick Start

1. **Activate Emscripten SDK**
   ```bash
   source /path/to/emsdk/emsdk_env.sh
   ```

2. **Build (Release)**
   ```bash
   cd web
   ./bootstrap-linux.sh
   ```

3. **Run Demo**
   ```bash
   # Start a local server (required for ES6 modules)
   cd demo
   python3 -m http.server 8080
   # Open http://localhost:8080 in browser
   ```

## Build Options

### Release Build (optimized for size)
```bash
./bootstrap-linux.sh
```

### Debug Build (with source maps)
```bash
./bootstrap-linux-debug.sh
```

### WASM64 Build (64-bit memory for large files)
```bash
./bootstrap-linux-wasm64.sh
```
Note: WASM64 requires Chrome 133+ or recent Firefox.

## Manual Build

```bash
# Release
emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -Bbuild
cmake --build build

# Debug with source maps
emcmake cmake -DCMAKE_BUILD_TYPE=Debug -DLIGHTUSD_WASM_DEBUG=ON -Bbuild
cmake --build build

# WASM64
emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DLIGHTUSD_WASM64=ON -Bbuild
cmake --build build
```

## Output Files

After building, files are placed in `js/src/lightusd/`:
- `lightusd.js` - JavaScript loader/glue code
- `lightusd.wasm` - WebAssembly binary
- `lightusd.d.ts` - TypeScript type definitions

## Usage in JavaScript

```javascript
import createLightUSD from './lightusd.js';

async function main() {
    const Module = await createLightUSD();

    console.log('LightUSD version:', Module.version());

    // Parse USDA string
    const usda = `#usda 1.0
    def Xform "World" {
        def Mesh "Cube" {
            float3[] extent = [(-1, -1, -1), (1, 1, 1)]
        }
    }`;

    const result = Module.readUsdaString(usda);
    if (result.ok()) {
        const stage = result.stage();
        console.log('Default prim:', stage.defaultPrim());
        console.log('Root prims:', stage.rootPrimCount());

        // Traverse hierarchy
        for (let i = 0; i < stage.rootPrimCount(); i++) {
            const prim = stage.rootPrim(i);
            console.log('Prim:', prim.name(), prim.typeName());
            prim.delete(); // Clean up
        }

        stage.delete();
    } else {
        console.error('Parse error:', result.error());
    }
    result.delete();
}

main();
```

## Usage in TypeScript

```typescript
import createLightUSD, { LightUSDModule, Stage, Prim } from './lightusd.js';

async function main(): Promise<void> {
    const Module: LightUSDModule = await createLightUSD();

    const result = Module.readUsdaString('#usda 1.0\ndef Xform "Root" {}');
    if (result.ok()) {
        const stage: Stage = result.stage();
        // ... use stage
        stage.delete();
    }
    result.delete();
}
```

## API Reference

### Module Functions

- `version()` - Returns LightUSD version string
- `readUsdaString(content)` - Parse USDA string, returns `UsdaReaderResult`

### Classes

#### Path
```javascript
const path = new Module.Path('/World/Mesh');
path.isValid();           // true
path.primPart();          // '/World/Mesh'
path.elementName();       // 'Mesh'
path.parent();            // Path('/World')
path.appendChild('Child'); // Path('/World/Mesh/Child')
path.delete();
```

#### Stage
```javascript
const stage = result.stage();
stage.defaultPrim();      // string
stage.upAxis();           // 'Y' or 'Z'
stage.rootPrimCount();    // number
stage.rootPrim(0);        // Prim
stage.primAtPath('/World'); // Prim
stage.toUsda();           // Export to USDA string
stage.delete();
```

#### Prim
```javascript
const prim = stage.rootPrim(0);
prim.name();              // 'World'
prim.typeName();          // 'Xform'
prim.path();              // Path
prim.childCount();        // number
prim.childNames();        // string[]
prim.child('Mesh');       // Prim
prim.propertyNames();     // string[]
prim.attribute('extent'); // Attribute
prim.delete();
```

#### Attribute
```javascript
const attr = prim.attribute('extent');
attr.name();              // 'extent'
attr.typeName();          // 'float3[]'
attr.hasValue();          // boolean
attr.value().toJS();      // Get value as JS type
attr.hasTimeSamples();    // boolean
attr.timeSampleTimes();   // number[]
attr.valueAtTime(1.0);    // Value at specific time
attr.delete();
```

## Memory Management

**Important**: All objects returned from the WASM module must be explicitly deleted to prevent memory leaks:

```javascript
const path = new Module.Path('/World');
// ... use path
path.delete(); // Required!
```

## Demos

### USDA Parser Demo
Open `demo/index.html` in a browser (via local server) to see an interactive USDA parser demo.

### WebGPU Rendering Demo
Open `demo/webgpu.html` for a WebGPU-based 3D viewer that renders USD meshes.

Features:
- Pure JavaScript WebGPU renderer (no three.js)
- Mesh triangulation in C++ (quads/ngons to triangles)
- Normal and tangent computation
- Orbit camera controls (drag to rotate, scroll to zoom)
- Live USDA editing with Ctrl+Enter to re-render

```bash
# Start local server
cd web
python3 -m http.server 8080
# Open http://localhost:8080/demo/webgpu.html
```

## RenderConverter API

Convert USD Stage to WebGPU-ready mesh data:

```javascript
import createLightUSD from './lightusd.js';

const Module = await createLightUSD();

// Parse USDA
const result = Module.readUsdaString(usdaContent);
const stage = result.stage();

// Convert to render data
const converter = new Module.RenderConverter();
const renderScene = converter.convert(stage, 0.0, true, true, true);
// Args: stage, time, triangulate, computeNormals, computeTangents

// Access mesh data as typed arrays (ready for WebGPU)
for (let i = 0; i < renderScene.meshCount(); i++) {
    const mesh = renderScene.mesh(i);

    // Get vertex data as Float32Array (zero-copy view into WASM memory)
    const positions = mesh.positions();  // vec3[]
    const normals = mesh.normals();      // vec3[] or null
    const texcoords = mesh.texcoords();  // vec2[] or null
    const tangents = mesh.tangents();    // vec4[] or null
    const indices = mesh.indices();      // Uint32Array

    // Bounding box
    const boundsMin = mesh.boundsMin();  // [x, y, z]
    const boundsMax = mesh.boundsMax();  // [x, y, z]

    // Create WebGPU buffers
    const positionBuffer = device.createBuffer({
        size: positions.byteLength,
        usage: GPUBufferUsage.VERTEX,
        mappedAtCreation: true
    });
    new Float32Array(positionBuffer.getMappedRange()).set(positions);
    positionBuffer.unmap();

    mesh.delete();
}

// Cleanup
renderScene.delete();
converter.delete();
stage.delete();
result.delete();
```

## Browser Compatibility

- Chrome 89+ (WASM)
- Firefox 89+ (WASM)
- Safari 15+ (WASM)
- Chrome 133+ (WASM64)
- Firefox 128+ (WASM64)

## License

Apache-2.0
