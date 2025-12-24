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

## Material Conversion

TinyUSDZ supports both UsdPreviewSurface and OpenPBR (MaterialX) materials. The library provides utilities to convert these materials to Three.js `MeshPhysicalMaterial`.

### Checking Material Type

```javascript
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

// Get material data as JSON
const materialData = usdScene.getMaterial(materialId, 'json');

// Check what material types are available
const typeInfo = TinyUSDZLoaderUtils.getMaterialType(materialData);
console.log(`Has OpenPBR: ${typeInfo.hasOpenPBR}`);
console.log(`Has UsdPreviewSurface: ${typeInfo.hasUsdPreviewSurface}`);
console.log(`Has both: ${typeInfo.hasBoth}`);
console.log(`Recommended: ${typeInfo.recommended}`);

// Or get a simple string representation
const typeString = TinyUSDZLoaderUtils.getMaterialTypeString(materialData);
// Returns: 'OpenPBR', 'UsdPreviewSurface', 'Both', or 'None'
```

### Converting Materials

```javascript
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

// Get material data as JSON
const materialData = usdScene.getMaterial(materialId, 'json');

// Smart conversion (auto-selects best material type)
// Prefers OpenPBR when both types are available
const material = await TinyUSDZLoaderUtils.convertMaterial(materialData, usdScene, {
    preferredMaterialType: 'auto',  // 'auto' | 'openpbr' | 'usdpreviewsurface'
    envMap: myEnvironmentMap,
    envMapIntensity: 1.0
});

// Force OpenPBR conversion
const openPBRMaterial = await TinyUSDZLoaderUtils.convertOpenPBRMaterialToMeshPhysicalMaterial(
    materialData, usdScene, { envMap: myEnvMap }
);

// Force UsdPreviewSurface conversion
const usdMaterial = TinyUSDZLoaderUtils.convertUsdMaterialToMeshPhysicalMaterial(
    materialData, usdScene
);
```

### Material Type Preference Options

| Option | Behavior |
|--------|----------|
| `'auto'` | Prefer OpenPBR when both are available (default) |
| `'openpbr'` | Force OpenPBR, fallback to UsdPreviewSurface if unavailable |
| `'usdpreviewsurface'` | Force UsdPreviewSurface, fallback to OpenPBR if unavailable |

### Supported OpenPBR Parameters

The OpenPBR to Three.js conversion supports the following parameter mappings:

| OpenPBR Layer | Parameters | Three.js Property |
|---------------|------------|-------------------|
| **Base** | `base_color` | `color`, `map` |
| | `base_metalness` | `metalness`, `metalnessMap` |
| **Specular** | `specular_roughness` | `roughness`, `roughnessMap` |
| | `specular_ior` | `ior` |
| | `specular_color` | `specularColor` |
| | `specular_anisotropy` | `anisotropy` |
| **Transmission** | `transmission_weight` | `transmission` |
| | `transmission_color` | `attenuationColor` |
| **Coat** | `coat_weight` | `clearcoat` |
| | `coat_roughness` | `clearcoatRoughness` |
| **Sheen/Fuzz** | `sheen_weight`, `fuzz_weight` | `sheen` |
| | `sheen_color`, `fuzz_color` | `sheenColor` |
| | `sheen_roughness`, `fuzz_roughness` | `sheenRoughness` |
| **Thin Film** | `thin_film_weight` | `iridescence` |
| | `thin_film_thickness` | `iridescenceThicknessRange` |
| | `thin_film_ior` | `iridescenceIOR` |
| **Emission** | `emission_color` | `emissive`, `emissiveMap` |
| | `emission_luminance` | `emissiveIntensity` |
| **Geometry** | `opacity`, `geometry_opacity` | `opacity`, `alphaMap` |
| | `normal`, `geometry_normal` | `normalMap` |

### Direct OpenPBR Class Usage

For manual material creation:

```javascript
import { TinyUSDZOpenPBR } from 'tinyusdz/TinyUSDZMaterialX.js';

// Create OpenPBR material manually
const openPBR = new TinyUSDZOpenPBR({
    baseColor: 0xff8844,
    metallic: 0.2,
    roughness: 0.6,
    emissive: 0x000000,
    emissiveIntensity: 0.0,
    opacity: 1.0,
    name: 'MyMaterial'
});

// Convert to Three.js MeshPhysicalMaterial
const threeMaterial = openPBR.toMeshPhysicalMaterial();
```

## UsdLux Light Support

TinyUSDZ supports USD lighting (UsdLux) with conversion to Three.js lights. The library handles various light types and environment maps.

### Supported Light Types

| USD Light Type | Three.js Equivalent | Notes |
|----------------|---------------------|-------|
| `SphereLight` | `PointLight` / `SpotLight` | SpotLight when shaping cone is defined |
| `DistantLight` | `DirectionalLight` | Infinite distance directional light |
| `RectLight` | `RectAreaLight` | Area light with width/height |
| `DiskLight` | `PointLight` | Approximated (no native Three.js equivalent) |
| `CylinderLight` | `PointLight` | Approximated (no native Three.js equivalent) |
| `DomeLight` | `HemisphereLight` + Environment | IBL/environment lighting |

### Accessing Light Data

```javascript
// Get number of lights
const numLights = usdScene.numLights();

// Get light data as JavaScript object
const light = usdScene.getLight(lightId);
console.log(light.type);      // 'point', 'sphere', 'distant', 'rect', 'disk', 'cylinder', 'dome'
console.log(light.color);     // [r, g, b] - linear RGB
console.log(light.intensity); // intensity multiplier
console.log(light.exposure);  // exposure value (EV)

// Get light data as JSON string (for serialization)
const lightJson = usdScene.getLightWithFormat(lightId, 'json');

// Get all lights at once
const allLights = usdScene.getAllLights();
```

### DomeLight Environment Maps

DomeLights can have HDR environment textures for image-based lighting (IBL):

```javascript
const light = usdScene.getLight(lightId);

if (light.type === 'dome') {
    console.log(light.textureFile);        // Asset path: "./textures/env.exr"
    console.log(light.envmapTextureId);    // Image ID: 0, or -1 if not loaded
    console.log(light.domeTextureFormat);  // 'automatic', 'latlong', 'mirroredBall', 'angular'
    console.log(light.guideRadius);        // Visualization radius

    // If envmapTextureId >= 0, the texture is loaded and available
    if (light.envmapTextureId >= 0) {
        const imageData = usdScene.getImage(light.envmapTextureId);
        console.log(`Envmap: ${imageData.width}x${imageData.height}`);
        console.log(`Channels: ${imageData.channels}`);
        console.log(`Decoded: ${imageData.decoded}`);
        // imageData.data contains raw pixel data (Uint8Array or Float32Array for HDR)
    }
}
```

### Three.js Environment Lighting Integration

The `usdlux.js` demo shows how to apply DomeLight envmaps to Three.js scenes:

```javascript
// Create Three.js texture from loaded image data
function createEnvMapFromUSD(usdScene, envmapTextureId) {
    const imageData = usdScene.getImage(envmapTextureId);
    if (!imageData || !imageData.decoded) return null;

    const { width, height, channels, data } = imageData;

    // Create float texture for HDR
    const floatData = new Float32Array(width * height * 4);
    // ... convert data to RGBA float ...

    const texture = new THREE.DataTexture(
        floatData, width, height,
        THREE.RGBAFormat, THREE.FloatType
    );
    texture.mapping = THREE.EquirectangularReflectionMapping;
    texture.colorSpace = THREE.LinearSRGBColorSpace;
    texture.needsUpdate = true;

    return texture;
}

// Apply to scene using PMREMGenerator
const pmremGenerator = new THREE.PMREMGenerator(renderer);
const envMap = pmremGenerator.fromEquirectangular(texture).texture;
scene.environment = envMap;  // PBR environment lighting
scene.background = envMap;   // Optional: use as background
pmremGenerator.dispose();
```

### Light Properties Reference

| Property | Type | Description |
|----------|------|-------------|
| `name` | string | Light prim name |
| `absPath` | string | Absolute USD prim path |
| `type` | string | Light type identifier |
| `color` | [r,g,b] | Linear RGB color |
| `intensity` | number | Intensity multiplier |
| `exposure` | number | Exposure value (EV stops) |
| `diffuse` | number | Diffuse contribution (0-1) |
| `specular` | number | Specular contribution (0-1) |
| `normalize` | boolean | Normalize by surface area |
| `enableColorTemperature` | boolean | Use color temperature |
| `colorTemperature` | number | Color temperature in Kelvin |
| `transform` | number[16] | World transformation matrix |
| `position` | [x,y,z] | World position |
| `direction` | [x,y,z] | Light direction (distant/spot) |
| `radius` | number | Sphere/Disk radius |
| `width` | number | RectLight width |
| `height` | number | RectLight height |
| `length` | number | CylinderLight length |
| `angle` | number | DistantLight angle (degrees) |
| `textureFile` | string | Texture asset path |
| `shapingConeAngle` | number | Spotlight cone angle |
| `shapingConeSoftness` | number | Cone edge softness |
| `shadowEnable` | boolean | Enable shadows |
| `shadowColor` | [r,g,b] | Shadow color |
| `domeTextureFormat` | string | Envmap format |
| `guideRadius` | number | DomeLight visualization radius |
| `envmapTextureId` | number | Index to images array (-1 if none) |

### CLI Light Dump Tool

Use `dump-usdlux-cli.js` to inspect USD lights:

```bash
# Dump all lights as summary
node dump-usdlux-cli.js scene.usda -f summary

# Dump as JSON with node hierarchy
node dump-usdlux-cli.js scene.usda -f json --show-nodes

# Show all details including transforms
node dump-usdlux-cli.js scene.usda -f summary --all
```

## Demo Pages

The following demo pages are available:

| Demo | File | Description |
|------|------|-------------|
| **MaterialX Demo** | `materialx.html` | Simple MaterialX/OpenPBR viewer with drag-and-drop USD loading, material parameter UI, and preset environment lighting |
| **MaterialX Debug Demo** | `mtlx-debug.html` | Advanced debugging demo with AOV visualization, node graph viewer, texture inspector, and comprehensive PBR debugging tools |
| **Animation Demo** | `animation.html` | USD animation playback demo |
| **Skinning Demo** | `skining-anim.html` | Skeletal animation and skinning demo |
| **UsdLux Demo** | `usdlux.html` | USD Lighting demo |
| **Basic Viewer** | `index.html` | Basic USD viewer with main.js |

### Running Demos

```bash
# Start development server
bun run dev

# Open in browser
# http://localhost:5173/materialx.html      # Simple MaterialX demo
# http://localhost:5173/mtlx-debug.html     # Advanced debug demo
```

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


