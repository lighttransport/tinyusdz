# PBR Debugging Tools

Material inspection and validation tools in the TinyUSDZ MaterialX web demos (`materialx.js`, `openpbr-nodegraph-demo.js`, `mtlx-debug.js`).

## Tools Overview

| Tool | Source | Purpose |
|------|--------|---------|
| Color Picker | `color-picker.js` | Sample final rendered colors from framebuffer |
| Material Property Picker | `material-property-picker.js` | Sample material values before lighting |
| Material JSON Viewer | `material-json-viewer.js` | Inspect Tydra-converted material data |
| Material Validator | `material-validator.js` | Automatic PBR error detection and linting |
| Material Override | `material-override.js` | Global property overrides for testing |
| Split View | `split-view-comparison.js` | Side-by-side material comparison |
| Texture Inspector | `texture-inspector.js` | Texture preview, tiling detection, colorspace |

## Color Picker

Reads pixels directly from the WebGL framebuffer via `gl.readPixels()`. Displays color in RGB (0-255), Hex, Float (0-1 sRGB), and Linear RGB (0-1) formats.

**Coordinate handling**: Mouse coords → devicePixelRatio scaling → Y-axis flip for WebGL origin (bottom-left). Shows both CSS and framebuffer pixel positions.

**sRGB ↔ Linear conversion**: Standard transfer function with 0.04045 threshold and gamma 2.4.

## Material Property Picker

Samples material properties **before lighting** using render-to-texture with custom shaders.

```
Pass 1: Base Color → baseColorTarget (custom shader outputs texture/constant color)
Pass 2: Material Props → materialPropsTarget (R=metalness, G=roughness)
Pass 3: Restore originals → screen
```

Uses `NearestFilter` for exact texel sampling. Supports JSON export of all picked properties.

**Key difference from Color Picker**: Material picker shows raw material values independent of lighting/tonemapping. Color picker shows the final rendered result.

## Material JSON Viewer

Tabbed interface showing material data at different pipeline stages:

1. **OpenPBR Surface** — full OpenPBR parameter groups (base, specular, transmission, coat, emission, geometry)
2. **UsdPreviewSurface** — USD standard material format
3. **Raw Material Data** — complete Tydra conversion output including texture IDs
4. **Three.js Material** — final MeshPhysicalMaterial properties with texture assignments

Syntax-highlighted JSON with copy-to-clipboard and download.

## Material Validator

Automatic checks for PBR best practices:

| Rule | Severity | Check |
|------|----------|-------|
| Energy Conservation | Warning | `baseColor * metalness ≤ 1.0` |
| IOR Range | Warning | `1.0 ≤ IOR ≤ 3.0` |
| Colorspace | Error | Base color maps use sRGB, data textures use Linear |
| Texture Power-of-Two | Info | Dimensions are 256/512/1024/2048/4096 |
| Zero Roughness | Info | Perfect mirrors (roughness < 0.01) are unrealistic |
| Intermediate Metalness | Info | Values between 0.1–0.9 (should be 0 or 1) |

## Material Override Presets

| Preset | Effect |
|--------|--------|
| `BASE_COLOR_ONLY` | Disable textures, roughness=0.5, metalness=0 |
| `NORMALS_ONLY` | Gray base, disable all except normal maps |
| `FLAT_SHADING` | Disable normal maps only |
| `MIRROR` | roughness=0, metalness=1 |
| `MATTE` | roughness=1, metalness=0 |
| `WHITE_CLAY` | Base color 0.8, roughness=0.6, no textures |

## AOV Modes

Visualize individual material properties by replacing shading with diagnostic views:

| Mode | What it Shows |
|------|---------------|
| Ambient Occlusion | White=exposed, black=occluded |
| Anisotropy | Hue=direction, brightness=strength |
| Sheen | RGB=sheen color × strength, alpha=roughness |
| Iridescence | R=strength, G=thickness, B=IOR |
| Normal Quality | Green=valid, yellow=warning, red=error (length ≠ 1.0) |
| UV Layout | R=U, G=V, white grid lines, red=UV seams |
| Shader Error | Green=valid, magenta=NaN, yellow=Inf, orange=overflow, cyan=negative |

## Debugging Workflow

1. **Validate**: Run `MaterialValidator` to check for errors
2. **Check normals**: `normal_quality` AOV (look for red)
3. **Check UVs**: `uv_layout` AOV (look for distortion/seams)
4. **Check textures**: Compare with/without using override presets
5. **Check colorspace**: Validate base color (sRGB) vs data textures (Linear)
6. **Check values**: Material Property Picker to sample exact pre-lighting values
7. **Compare**: Split View to see before/after changes
