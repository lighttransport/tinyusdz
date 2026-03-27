# UsdLux Lighting Support (WASM / JS)

This document covers the JavaScript/WASM side of UsdLux lighting in TinyUSDZ: the Emscripten bindings, JS utility APIs, Three.js integration, and CLI tools.

For C++ core and Tydra internals, see [doc/usdLux.md](../../../doc/usdLux.md).

## Source Files

| File | Description |
|------|-------------|
| `web/binding.cc` | Emscripten bindings: `getLight()`, `numLights()`, `getAllLights()`, `getLightWithFormat()` |
| `web/js/src/tinyusdz/TinyUSDZLoaderUtils.js` | DomeLight loading, PMREM generation, texture helpers |
| `web/js/src/tinyusdz/SceneHelpers.js` | Light visualization helpers for Three.js |
| `web/js/usdlux.js` | Interactive UsdLux demo (Three.js scene, spectral, HDRI projection) |
| `web/js/light-hdri-projection.js` | Analytical light-to-HDRI projection engine |
| `web/js/cli/dump-usdlux-cli.js` | CLI tool: dump light data (JSON/YAML/summary/XML) |
| `web/js/cli/test-usdlux-parsing.js` | Automated light parsing tests |

---

## WASM API

The native WASM module exposes four methods on the loaded USD scene object:

### `numLights() → number`

Returns the number of lights in the scene.

### `getLight(lightId) → object`

Returns a JavaScript object with all light properties for the given index.

**Returned object fields:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Light prim name |
| `absPath` | string | Absolute USD prim path |
| `displayName` | string | Display name |
| `type` | string | `"point"`, `"sphere"`, `"disk"`, `"rect"`, `"cylinder"`, `"distant"`, `"dome"`, `"geometry"`, `"portal"` |
| `color` | number[3] | Linear RGB color [0-1] |
| `intensity` | number | Intensity multiplier |
| `exposure` | number | Exposure value (EV stops) |
| `diffuse` | number | Diffuse contribution (0-1) |
| `specular` | number | Specular contribution (0-1) |
| `normalize` | boolean | Normalize by surface area |
| `enableColorTemperature` | boolean | Use color temperature |
| `colorTemperature` | number | Color temperature in Kelvin |
| `transform` | number[16] | 4x4 world transformation matrix (column-major) |
| `position` | number[3] | World position [x,y,z] |
| `direction` | number[3] | Light direction [x,y,z] |
| `radius` | number | Sphere/Disk radius |
| `width` | number | RectLight width |
| `height` | number | RectLight height |
| `length` | number | CylinderLight length |
| `angle` | number | DistantLight angular diameter (degrees) |
| `textureFile` | string | Texture asset path (Dome/Rect) |
| `shapingConeAngle` | number | Spotlight cone angle (degrees) |
| `shapingConeSoftness` | number | Cone edge softness |
| `shapingFocus` | number | Focus adjustment |
| `shapingFocusTint` | number[3] | Focus tint color |
| `shapingIesFile` | string | IES profile file path |
| `shapingIesAngleScale` | number | IES angle scale |
| `shapingIesNormalize` | boolean | Normalize IES profile |
| `shadowEnable` | boolean | Enable shadows |
| `shadowColor` | number[3] | Shadow color |
| `shadowDistance` | number | Shadow distance (-1 = infinite) |
| `shadowFalloff` | number | Shadow falloff (-1 = none) |
| `shadowFalloffGamma` | number | Shadow falloff gamma |
| `domeTextureFormat` | string | `"automatic"`, `"latlong"`, `"mirroredBall"`, `"angular"` |
| `guideRadius` | number | DomeLight visualization radius |
| `envmapTextureId` | number | Index into images array (-1 = none) |
| `geometryMeshId` | number | GeometryLight mesh index |
| `materialSyncMode` | string | MeshLightAPI sync mode |
| `spectralEmission` | object\|null | LTE SpectralAPI data: `{samples, interpolation, unit, preset}` |

### `getLightWithFormat(lightId, format) → object`

Returns serialized light data.

- `format`: `"json"` or `"xml"`
- Returns: `{ data: string, format: string }` on success, `{ error: string }` on failure

### `getAllLights() → object[]`

Returns an array of all light objects (same shape as `getLight()` output).

---

## Three.js Integration

### Light Type Mapping

| USD Type | Three.js Class | Notes |
|----------|---------------|-------|
| `sphere` / `point` | `PointLight` or `SpotLight` | SpotLight when `shapingConeAngle < 90` |
| `distant` | `DirectionalLight` | Position derived from transform quaternion |
| `rect` | `RectAreaLight` | Requires `RectAreaLightUniformsLib` init |
| `disk` | `RectAreaLight` | Approximated as square with `radius * 2` side |
| `cylinder` | `PointLight` | Approximated (no native equivalent) |
| `dome` | `HemisphereLight` + environment | Sets `scene.environment` via PMREM |

### Intensity Calculation

Effective intensity = `intensity * 2^exposure`

### Transform Handling

The 4x4 transform matrix is decomposed into position, quaternion, and scale using `THREE.Matrix4.decompose()`. USD lights face -Z in local space by default.

### Shadow Configuration

Shadows are enabled for DirectionalLight, SpotLight, and PointLight:
- Shadow map size: 1024x1024
- Shadow bias: -0.0001
- Camera frustum auto-configured per light type

---

## DomeLight / Environment Map Pipeline

`TinyUSDZLoaderUtils` provides a multi-stage pipeline for loading DomeLight environment maps:

### `loadDomeLightFromUSD(usdLoader, pmremGenerator) → object|null`

Main entry point. Iterates all lights, finds the first DomeLight, and processes it.

### Processing Order

1. **`loadDomeLightFromTextureId()`** - If `envmapTextureId >= 0`, loads the texture from the WASM image buffer. Creates a `THREE.DataTexture` (float) and processes through PMREM.

2. **`loadDomeLightFromFile()`** - If a `textureFile` path is set but no embedded texture, attempts HTTP fetch. Supports EXR (via `THREE.EXRLoader`), HDR (via `THREE.RGBELoader`), and standard image formats.

3. **`loadDomeLightAsConstantColor()`** - Fallback when no texture is available. Creates a solid-color environment from the light's `color` property.

### Result Object

```javascript
{
  texture: THREE.Texture,     // PMREM-processed environment map
  intensity: number,          // Computed intensity (intensity * 2^exposure)
  name: string,               // Light name
  color: [r, g, b],           // Light color
  // ... other light properties
}
```

### Usage

```javascript
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

const pmremGenerator = new THREE.PMREMGenerator(renderer);
const dome = await TinyUSDZLoaderUtils.loadDomeLightFromUSD(usdLoader, pmremGenerator);

if (dome) {
  scene.environment = dome.texture;
  scene.background = dome.texture;  // optional
}
pmremGenerator.dispose();
```

---

## Scene Helpers (`SceneHelpers.js`)

### `createPointLightHelper({ color, scale })`

Creates a Blender-style wireframe sphere with 3 dashed orbit circles for visualizing point/sphere lights in the scene.

### `attachSceneHelpers(threeRoot, usdLoader, { scale, showLights, showCameras })`

Traverses the Three.js scene graph and attaches visual helpers to nodes tagged with `nodeCategory` metadata. Returns an array of helper groups for toggling visibility.

---

## HDRI Projection (`light-hdri-projection.js`)

Analytical projection of USD lights into 360-degree HDR environment maps. Used by the `usdlux.js` demo for real-time light-to-HDRI conversion.

### Light Classes

| Class | Description |
|-------|-------------|
| `SphereLight` | Spherical area light with solid angle calculation |
| `AreaLight` | Rectangular area light with width/height |
| `DiskLight` | Circular area light |
| `PointLight` | Omnidirectional point source |
| `DistantLight` | Directional (sun) light |

### `LightHDRIProjection`

Main projection engine:

```javascript
const projection = new LightHDRIProjection();
projection.addLight(projLight);
const hdri = projection.render(width, height, samples);
// hdri is a Float32Array of RGBA pixels
```

Output can be exported as 32-bit float EXR via the included `writeEXR()` function.

---

## CLI Tools

### `dump-usdlux-cli.js`

```bash
node cli/dump-usdlux-cli.js <usd-file> [options]
```

| Option | Description |
|--------|-------------|
| `-f, --format` | Output format: `json`, `yaml`, `summary`, `xml` |
| `-o, --output` | Write output to file |
| `-l, --light` | Dump specific light by index |
| `-t, --show-transform` | Include 4x4 transform matrices |
| `-s, --show-spectral` | Include LTE SpectralAPI data |
| `-n, --show-nodes` | Show node hierarchy with categories |
| `-a, --all` | Show all optional data |
| `--color-mode` | Color display: `rgb`, `hex`, `kelvin` |
| `--serialized` | Use WASM serialized format (JSON/XML) |

### `test-usdlux-parsing.js`

Automated test suite validating light parsing against test USD files in `tests/feat/lux/`:

```bash
node cli/test-usdlux-parsing.js
```

Test cases cover basic lights, shaping/IES, mesh lights, and multi-light scenes.

---

## Spectral Emission (Extension)

The `usdlux.js` demo includes spectral rendering support:

- CIE 1931 color matching functions (X, Y, Z) for wavelength-to-RGB conversion
- D65, A, D50, E, F1-F11 standard illuminant presets
- Planckian blackbody SPD generation
- CIE daylight SPD model
- Real-time spectral curve visualization in the UI
- Three.js material colors updated from SPD integration

Spectral data from the WASM `spectralEmission` field (when present) feeds directly into this pipeline.

---

## Test USD Files

Located in `tests/feat/lux/`:

| File | Contents |
|------|----------|
| `01_basic_uniform_light.usda` | Basic point light |
| `02_conical_spotlight.usda` | Spotlight with cone angle |
| `03_measured_ies_light.usda` | IES profile light |
| `04_complete_scene.usda` | Three-point lighting setup |
| `06_mesh_lights.usda` | Geometry/mesh lights with MeshLightAPI |
| `07_animated_mesh_lights.usda` | Animated mesh lights |

---

## Data Flow Summary

```
USD file
  → WASM: parse & reconstruct light prims
  → WASM: Tydra ConvertXxxLight() → RenderLight
  → Emscripten binding: getLight() / getAllLights()
  → JS object with all properties
  → Three.js light creation + shadow config
  → Optional: DomeLight → PMREM environment map
  → Optional: HDRI projection → Float32 EXR
  → Optional: Spectral SPD → RGB visualization
```
