# MaterialX Support in TinyUSDZ

This document describes the MaterialX integration, color space support, and implementation roadmap for complete MaterialX support in TinyUSDZ.

## Overview

TinyUSDZ provides comprehensive support for MaterialX, including a full suite of color space conversions required for proper MaterialX document processing. The library can parse MaterialX (.mtlx) files and handle all standard MaterialX color spaces. This document also outlines the current state of MaterialX support and provides a comprehensive todo list for complete MaterialX and MaterialXConfigAPI implementation in both the core library and Tydra render material conversion pipeline.

**New in this document:** Comprehensive Blender 4.5+ MaterialX export documentation, including complete Principled BSDF to OpenPBR Surface parameter mapping tables with conversion formulas and usage notes for production pipelines.

## Color Space Support

### Supported Color Spaces

TinyUSDZ supports all major color spaces used in MaterialX documents:

| Color Space | Enum Value | Description |
|------------|------------|-------------|
| `srgb` | `ColorSpace::sRGB` | Standard RGB with sRGB transfer function |
| `lin_srgb` | `ColorSpace::Lin_sRGB` | Linear sRGB (no gamma) |
| `srgb_texture` | `ColorSpace::sRGB_Texture` | sRGB for texture inputs |
| `rec709` | `ColorSpace::Rec709` | Rec.709 with gamma |
| `lin_rec709` | `ColorSpace::Lin_Rec709` | Linear Rec.709 (MaterialX default) |
| `g22_rec709` | `ColorSpace::g22_Rec709` | Rec.709 with gamma 2.2 |
| `g18_rec709` | `ColorSpace::g18_Rec709` | Rec.709 with gamma 1.8 |
| `lin_rec2020` | `ColorSpace::Lin_Rec2020` | Linear Rec.2020/Rec.2100 |
| `acescg` / `lin_ap1` | `ColorSpace::Lin_ACEScg` | ACES CG (AP1 primaries) |
| `aces2065-1` | `ColorSpace::ACES2065_1` | ACES 2065-1 (AP0 primaries) |
| `lin_displayp3` | `ColorSpace::Lin_DisplayP3` | Linear Display P3 |
| `srgb_displayp3` | `ColorSpace::sRGB_DisplayP3` | Display P3 with sRGB transfer |
| `raw` | `ColorSpace::Raw` | No color space (data textures) |

### Color Space Conversion Functions

#### sRGB Conversions
```cpp
// 8-bit sRGB ↔ Linear conversions
bool srgb_8bit_to_linear_f32(const std::vector<uint8_t> &in_img, ...);
bool linear_f32_to_srgb_8bit(const std::vector<float> &in_img, ...);

// Float32 sRGB ↔ Linear conversions  
bool srgb_f32_to_linear_f32(const std::vector<float> &in_img, ...);
```

#### Rec.709 Conversions
```cpp
// Rec.709 with standard gamma
bool rec709_8bit_to_linear_f32(const std::vector<uint8_t> &in_img, ...);

// Note: lin_rec709 has the same primaries as sRGB/Rec.709, 
// so no color space conversion is needed, only gamma
```

#### Rec.2020 Conversions
```cpp
// Rec.2020 gamma ↔ linear conversions
bool rec2020_8bit_to_linear_f32(const std::vector<uint8_t> &in_img, ...);
bool linear_f32_to_rec2020_8bit(const std::vector<float> &in_img, ...);

// Rec.2020 ↔ sRGB color gamut conversions
bool linear_rec2020_to_linear_sRGB(const std::vector<float> &in_img, ...);
bool linear_sRGB_to_linear_rec2020(const std::vector<float> &in_img, ...);
```

#### Gamma Conversions
```cpp
// Gamma 2.2 conversions (for g22_rec709)
bool gamma22_f32_to_linear_f32(const std::vector<float> &in_img, ...);
bool linear_f32_to_gamma22_f32(const std::vector<float> &in_img, ...);

// Gamma 1.8 conversions (for g18_rec709)
bool gamma18_f32_to_linear_f32(const std::vector<float> &in_img, ...);
bool linear_f32_to_gamma18_f32(const std::vector<float> &in_img, ...);
```

#### ACES Conversions
```cpp
// ACEScg (AP1) conversions
bool linear_sRGB_to_ACEScg(const std::vector<float> &in_img, ...);
bool ACEScg_to_linear_sRGB(const std::vector<float> &in_img, ...);

// ACES 2065-1 (AP0) conversions
bool linear_sRGB_to_ACES2065_1(const std::vector<float> &in_img, ...);
bool ACES2065_1_to_linear_sRGB(const std::vector<float> &in_img, ...);
```

#### Display P3 Conversions
```cpp
// Display P3 conversions
bool linear_displayp3_to_linear_sRGB(const std::vector<float> &in_img, ...);
bool linear_sRGB_to_linear_displayp3(const std::vector<float> &in_img, ...);
bool displayp3_f16_to_linear_f32(const std::vector<value::half> &in_img, ...);
```

## MaterialX Integration

### MaterialX Parser

TinyUSDZ includes a MaterialX parser located in `sandbox/mtlx-parser/` that can:
- Parse MaterialX XML documents (.mtlx files)
- Extract document-level colorspace settings
- Parse element-level colorspace attributes
- Handle MaterialX node graphs and material definitions

### Color Space in MaterialX Files

MaterialX files typically specify color spaces at multiple levels:

1. **Document Level**: Set in the root `<materialx>` element
   ```xml
   <materialx version="1.39" colorspace="lin_rec709">
   ```

2. **Texture Level**: Specified on `<image>` and `<tiledimage>` nodes
   ```xml
   <image name="diffuse_tex" type="color3" colorspace="srgb_texture">
   ```

3. **Value Level**: Can be specified on individual inputs
   ```xml
   <input name="opacity" type="float" value="0.5" colorspace="lin_rec709"/>
   ```

### Usage Example

```cpp
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "image-util.hh"

// Load a USD file with MaterialX materials
tinyusdz::Stage stage;
std::string warn, err;
bool ret = tinyusdz::LoadUSDFromFile("model_with_mtlx.usd", &stage, &warn, &err);

// The color space is automatically inferred from MaterialX metadata
tinyusdz::tydra::ColorSpace colorSpace;
tinyusdz::value::token colorSpaceToken("lin_rec709");
if (tinyusdz::tydra::InferColorSpace(colorSpaceToken, &colorSpace)) {
    // colorSpace is now ColorSpace::Lin_Rec709
}

// Convert textures to the appropriate color space
std::vector<uint8_t> srgb_texture_data = LoadTexture("diffuse.png");
std::vector<float> linear_data;

// Convert from sRGB texture space to linear for rendering
tinyusdz::srgb_8bit_to_linear_f32(
    srgb_texture_data, 
    width, height, 
    3, 3,  // RGB channels
    &linear_data
);
```

## Blender MaterialX Export Support (4.5+)

### Overview

Starting with Blender 4.5 LTS, the USD/MaterialX exporter writes Principled BSDF materials as OpenPBR Surface shading nodes, which provides significantly better compatibility than the previous Standard Surface approach. The Principled BSDF shader in Blender is based on the OpenPBR Surface shading model, making the parameter mapping more natural and accurate.

### Export Behavior

When MaterialX export is enabled in Blender's USD exporter:
- **Dual Export**: Both MaterialX (OpenPBR) and UsdPreviewSurface networks are exported on the same USD Material
- **Fallback Support**: Renderers that don't support MaterialX can fall back to UsdPreviewSurface
- **Better Matching**: Coat, emission, and sheen parameters more closely match Cycles renderer with OpenPBR export
- **Known Limitations**: Anisotropy conversion remains challenging (neither old nor new conversion is a perfect match)

### Principled BSDF to OpenPBR Parameter Mapping

Blender's Principled BSDF uses slightly different naming conventions than OpenPBR. Below is the comprehensive parameter mapping:

#### Base Layer

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Base Color** | `base_color` | Direct mapping - Diffuse/metallic base color |
| **Weight** | `base_weight` | Overall multiplier for base layer |
| **Diffuse Roughness** | `base_diffuse_roughness` | Oren-Nayar roughness (0 = Lambertian) |
| **Metallic** | `base_metalness` | Mix weight between metal and dielectric (0-1) |

#### Specular Layer

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **IOR** | `specular_ior` | Index of refraction (default: 1.5 for glass) |
| **IOR Level** | `specular_weight` | **Conversion: multiply by 2.0** - Blender uses 0.5 as neutral, OpenPBR uses 1.0 |
| **Specular Tint** | `specular_color` | Color tint for dielectric Fresnel reflection |
| **Roughness** | `specular_roughness` | Microfacet distribution roughness (0-1) |
| **Anisotropic** | `specular_roughness_anisotropy` | Stretches microfacet distribution (0-1) |
| **Anisotropic Rotation** | *(tangent vector)* | **Complex**: OpenPBR uses tangent rotation instead of explicit parameter |
| **Tangent** | `geometry_tangent` | Anisotropy direction reference |

#### Subsurface Scattering

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Subsurface Weight** | `subsurface_weight` | Direct mapping - Mix between SSS and diffuse (0-1) |
| **Subsurface Scale** | `subsurface_radius` | Mean free path scale |
| **Subsurface Radius** | `subsurface_radius_scale` | Per-channel RGB multiplier |
| **Subsurface IOR** | `specular_ior` | Uses same IOR as specular layer |
| **Subsurface Anisotropy** | `subsurface_scatter_anisotropy` | Phase function directionality (-1 to 1) |

#### Transmission (Translucency)

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Transmission Weight** | `transmission_weight` | Mix between translucent and opaque (0-1) |
| **Transmission Color** | `transmission_color` | Extinction coefficient color |
| **Transmission Depth** | `transmission_depth` | Distance for color attenuation |
| *(N/A)* | `transmission_scatter` | OpenPBR-specific: interior scattering coefficient |
| *(N/A)* | `transmission_scatter_anisotropy` | OpenPBR-specific: scatter directionality |
| *(N/A)* | `transmission_dispersion_scale` | OpenPBR-specific: chromatic dispersion amount |
| *(N/A)* | `transmission_dispersion_abbe_number` | OpenPBR-specific: physical Abbe number |

#### Coat Layer (Clearcoat)

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Coat Weight** | `coat_weight` | **Renamed** from "Clearcoat" in Blender 4.0+ |
| **Coat Tint** | `coat_color` | Color tint for coat layer |
| **Coat Roughness** | `coat_roughness` | Coat surface roughness (default: 0.03) |
| **Coat IOR** | `coat_ior` | Coat refractive index (default: 1.5) |
| *(N/A)* | `coat_roughness_anisotropy` | OpenPBR-specific: coat anisotropy direction |
| **Coat Normal** | `geometry_coat_normal` | Separate normal map for coat |
| *(N/A)* | `geometry_coat_tangent` | OpenPBR-specific: coat anisotropy tangent |
| *(N/A)* | `coat_affect_color` | OpenPBR-specific: saturation effect on base |
| *(N/A)* | `coat_affect_roughness` | OpenPBR-specific: roughness modification |

#### Sheen Layer (Fuzz)

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Sheen Weight** | `fuzz_weight` | **Renamed**: "sheen" in Blender, "fuzz" in OpenPBR |
| **Sheen Tint** | `fuzz_color` | **Renamed**: color → tint mapping |
| **Sheen Roughness** | `fuzz_roughness` | Microfiber surface roughness (default: 1.0) |

#### Thin Film (Iridescence)

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Thin Film Weight** | `thin_film_weight` | Film coverage/presence (0-1) |
| **Thin Film Thickness** | `thin_film_thickness` | Thickness in micrometers (default: 0.5 μm) |
| **Thin Film IOR** | `thin_film_ior` | Film refractive index |

#### Emission

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Emission Color** | `emission_color` | Direct mapping - emissive color |
| **Emission Strength** | `emission_luminance` | Luminance intensity |

#### Geometry & Opacity

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| **Alpha** | `geometry_opacity` | Overall transparency (0-1) |
| **Normal** | `geometry_normal` | Base surface normal map |

### Key Conversion Notes

#### 1. Specular IOR Level Conversion
The most important conversion is for specular intensity:
```
OpenPBR specular_weight = Blender IOR_Level × 2.0
```
- **Blender**: 0.5 = neutral (no change), 0 = no reflections, 1.0 = doubled reflections
- **OpenPBR**: 1.0 = standard reflections, 0 = no reflections, >1.0 = increased reflections

#### 2. Anisotropic Rotation Challenge
Blender's **Anisotropic Rotation** parameter (0-1 angle) doesn't directly map to OpenPBR's tangent vector approach:
- **Blender**: Uses rotation angle around normal
- **OpenPBR**: Uses explicit tangent vector for orientation
- **Export Solution**: Blender rotates the tangent vector around the normal using the rotation value

#### 3. Parameter Renaming Summary
- `fuzz` (OpenPBR) ↔ `sheen` (Blender)
- `color` (OpenPBR) ↔ `tint` (Blender) in various contexts
- `specular_weight` (OpenPBR) ↔ `IOR Level` (Blender)
- `coat` (OpenPBR/Blender 4.0+) ↔ `clearcoat` (older Blender)

#### 4. Missing Blender Parameters
OpenPBR includes several parameters not exposed in Blender's Principled BSDF:
- `coat_affect_color` - Coat saturation effect
- `coat_affect_roughness` - Coat roughness modification
- `coat_roughness_anisotropy` - Anisotropic coat
- `transmission_scatter` - Interior scattering
- `transmission_dispersion_*` - Chromatic dispersion

These are set to defaults when exporting from Blender.

### Export Quality Notes

Based on Blender 4.5 development:
- ✅ **Improved**: Coat, emission, and sheen match Cycles more accurately
- ⚠️ **Challenging**: Anisotropy conversion is approximate (formulas differ between systems)
- ⚠️ **Approximate**: IOR Level requires 2× scaling
- ✅ **Good**: Overall material appearance is well-preserved

### Usage in Production Pipelines

**Enable MaterialX Export in Blender:**
1. File → Export → Universal Scene Description (.usd/.usdc/.usda)
2. Check "MaterialX" option in export settings
3. Materials will be exported as both OpenPBR and UsdPreviewSurface

**Benefits:**
- **Interoperability**: Works across Maya, Houdini, USD Hydra renderers
- **Fallback**: UsdPreviewSurface ensures broad compatibility
- **Accuracy**: OpenPBR more closely matches Blender's Cycles renderer

**Limitations:**
- MaterialX export is experimental (off by default in 4.5)
- Complex node setups may not fully translate
- Custom nodes require manual MaterialX equivalent

### Related Blender Features

**Blender 4.5 USD Export Improvements:**
- Point Instancing support through Geometry Nodes
- Text object export (as mesh data)
- `UsdPrimvarReader` support for `Attribute` nodes

**MaterialX Version Support:**
- MaterialX 1.39.0+ includes OpenPBR Surface
- MaterialX 1.39.1 added Standard Surface ↔ OpenPBR translation graphs

## Three.js / WebGL MaterialX Integration

TinyUSDZ provides JavaScript APIs for converting OpenPBR/MaterialX materials to Three.js materials. Two material implementations are available:

### Material Implementations

| Implementation | Class | Use Case |
|---------------|-------|----------|
| **MeshPhysicalMaterial** | `THREE.MeshPhysicalMaterial` | Standard Three.js PBR material, broad compatibility |
| **OpenPBRMaterial** | Custom `ShaderMaterial` | Full OpenPBR BRDF with Oren-Nayar diffuse, coat IOR, fuzz layer |

### MeshPhysicalMaterial Conversion

Converts OpenPBR parameters to Three.js MeshPhysicalMaterial properties.

#### Supported Parameters

| OpenPBR Parameter | MeshPhysicalMaterial Property | Notes |
|------------------|------------------------------|-------|
| `base_color` | `color` | Diffuse/albedo color |
| `base_metalness` | `metalness` | Metallic factor (0-1) |
| `specular_roughness` | `roughness` | Surface roughness (0-1) |
| `specular_ior` | `ior` | Index of refraction |
| `specular_color` | `specularColor` | Specular tint |
| `specular_anisotropy` | `anisotropy` | Anisotropic stretching |
| `transmission_weight` | `transmission` | Transmission factor |
| `transmission_color` | `attenuationColor` | Transmission color |
| `coat_weight` | `clearcoat` | Clearcoat intensity |
| `coat_roughness` | `clearcoatRoughness` | Clearcoat roughness |
| `sheen_weight` / `fuzz_weight` | `sheen` | Sheen intensity |
| `sheen_color` / `fuzz_color` | `sheenColor` | Sheen tint |
| `sheen_roughness` / `fuzz_roughness` | `sheenRoughness` | Sheen roughness |
| `thin_film_weight` | `iridescence` | Iridescence intensity |
| `thin_film_thickness` | `iridescenceThicknessRange` | Film thickness |
| `thin_film_ior` | `iridescenceIOR` | Film IOR |
| `emission_color` | `emissive` | Emission color |
| `emission_luminance` | `emissiveIntensity` | Emission strength |
| `geometry_opacity` / `opacity` | `opacity` | Alpha value |
| `geometry_normal` / `normal` | `normalMap` | Normal map texture |

#### Supported Texture Maps

| OpenPBR Texture | MeshPhysicalMaterial Map | Channel |
|-----------------|-------------------------|---------|
| `base_color` | `map` | RGB |
| `specular_roughness` | `roughnessMap` | G channel |
| `base_metalness` | `metalnessMap` | B channel |
| `emission_color` | `emissiveMap` | RGB |
| `geometry_normal` | `normalMap` | RGB (tangent space) |
| `geometry_opacity` | `alphaMap` | Single channel |

#### API Functions

```javascript
import {
    convertOpenPBRToMeshPhysicalMaterial,
    convertOpenPBRToMeshPhysicalMaterialLoaded
} from 'tinyusdz/TinyUSDZMaterialX.js';

// Returns immediately, textures load in background (fire-and-forget)
const material = convertOpenPBRToMeshPhysicalMaterial(materialData, usdScene, options);

// Waits for all textures to load before returning
const material = await convertOpenPBRToMeshPhysicalMaterialLoaded(materialData, usdScene, options);
```

### OpenPBRMaterial (Custom Shader)

Full OpenPBR BRDF implementation as a Three.js ShaderMaterial, supporting features not available in MeshPhysicalMaterial.

#### Additional Features vs MeshPhysicalMaterial

| Feature | OpenPBRMaterial | MeshPhysicalMaterial |
|---------|----------------|---------------------|
| Oren-Nayar Diffuse | ✅ `base_diffuse_roughness` | ❌ Lambertian only |
| Coat Color | ✅ `coat_color` | ❌ White only |
| Coat IOR | ✅ `coat_ior` | ❌ Fixed 1.5 |
| Fuzz Layer | ✅ OpenPBR formulation | ⚠️ Approximated as sheen |
| Thin Film Physics | ✅ Interference simulation | ⚠️ Simplified |

#### Supported Parameters

| OpenPBR Parameter | Uniform Name | Default |
|------------------|--------------|---------|
| **Base Layer** | | |
| `base_weight` | `base_weight` | 1.0 |
| `base_color` | `base_color` | (0.8, 0.8, 0.8) |
| `base_metalness` | `base_metalness` | 0.0 |
| `base_diffuse_roughness` | `base_diffuse_roughness` | 0.0 |
| **Specular Layer** | | |
| `specular_weight` | `specular_weight` | 1.0 |
| `specular_color` | `specular_color` | (1.0, 1.0, 1.0) |
| `specular_roughness` | `specular_roughness` | 0.3 |
| `specular_ior` | `specular_ior` | 1.5 |
| `specular_anisotropy` | `specular_anisotropy` | 0.0 |
| `specular_rotation` | `specular_rotation` | 0.0 |
| **Coat Layer** | | |
| `coat_weight` | `coat_weight` | 0.0 |
| `coat_color` | `coat_color` | (1.0, 1.0, 1.0) |
| `coat_roughness` | `coat_roughness` | 0.0 |
| `coat_ior` | `coat_ior` | 1.5 |
| **Fuzz Layer** | | |
| `fuzz_weight` | `fuzz_weight` | 0.0 |
| `fuzz_color` | `fuzz_color` | (1.0, 1.0, 1.0) |
| `fuzz_roughness` | `fuzz_roughness` | 0.5 |
| **Thin Film** | | |
| `thin_film_weight` | `thin_film_weight` | 0.0 |
| `thin_film_thickness` | `thin_film_thickness` | 500.0 nm |
| `thin_film_ior` | `thin_film_ior` | 1.5 |
| **Transmission** | | |
| `transmission_weight` | `transmission_weight` | 0.0 |
| `transmission_color` | `transmission_color` | (1.0, 1.0, 1.0) |
| **Emission** | | |
| `emission_luminance` | `emission_luminance` | 0.0 |
| `emission_color` | `emission_color` | (1.0, 1.0, 1.0) |
| **Geometry** | | |
| `geometry_opacity` | `geometry_opacity` | 1.0 |

#### Supported Texture Maps

| OpenPBR Texture | Property | Shader Define |
|-----------------|----------|---------------|
| `base_color` | `map` | `USE_MAP` |
| `specular_roughness` | `roughnessMap` | `USE_ROUGHNESSMAP` |
| `base_metalness` | `metalnessMap` | `USE_METALNESSMAP` |
| `emission_color` | `emissiveMap` | `USE_EMISSIVEMAP` |
| `geometry_normal` | `normalMap` | `USE_NORMALMAP` |
| `ambient_occlusion` | `aoMap` | `USE_AOMAP` |

#### API Functions

```javascript
// In materialx.js demo

// Returns immediately, textures load in background
const material = convertToOpenPBRMaterial(matData, nativeLoader);

// Waits for all textures to load before returning
const material = await convertToOpenPBRMaterialLoaded(matData, nativeLoader);
```

### Texture Loading Patterns

Two loading patterns are available for both material types:

| Pattern | Function Suffix | Behavior | Use Case |
|---------|----------------|----------|----------|
| **Immediate** | (none) | Returns material immediately, textures load asynchronously | Interactive loading, progressive display |
| **Loaded** | `Loaded` | Awaits all textures before returning | Batch rendering, screenshots |

#### Example: Immediate Pattern
```javascript
// Material appears immediately with base colors
// Textures pop in as they load
const material = convertOpenPBRToMeshPhysicalMaterial(data, scene);
mesh.material = material;
// Render loop continues, textures appear when ready
```

#### Example: Loaded Pattern
```javascript
// Wait for complete material with all textures
const material = await convertOpenPBRToMeshPhysicalMaterialLoaded(data, scene);
mesh.material = material;
// All textures are ready before first render
```

### HDR/EXR Texture Support

Both material converters support HDR and EXR texture formats:

| Format | Decoder | Fallback |
|--------|---------|----------|
| HDR (Radiance) | TinyUSDZ WASM (faster) | Three.js HDRLoader |
| EXR (OpenEXR) | Three.js EXRLoader | TinyUSDZ (for unsupported compression) |

```javascript
// Initialize TinyUSDZ module reference for HDR/EXR fallback
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { setTinyUSDZ } from 'tinyusdz/TinyUSDZMaterialX.js';

// After TinyUSDZ WASM initialization
TinyUSDZLoaderUtils.setTinyUSDZ(tinyusdzModule);
setTinyUSDZ(tinyusdzModule);
```

### Usage Example

```javascript
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { OpenPBRMaterial } from './OpenPBRMaterial.js';

// Initialize loader
const loader = new TinyUSDZLoader();
await loader.init();

// Set TinyUSDZ reference for HDR/EXR support
TinyUSDZLoaderUtils.setTinyUSDZ(loader.native_);

// Load USD file
const usd = await loader.loadAsync('model.usdz');

// Convert materials using TinyUSDZLoaderUtils
const material = await TinyUSDZLoaderUtils.convertMaterial(
    materialData,
    usd,
    {
        preferredMaterialType: 'physical', // or 'openpbr'
        envMap: envTexture,
        envMapIntensity: 1.0
    }
);
```

## MaterialX NodeGraph and Node Shaders

TinyUSDZ supports MaterialX node graphs as used in USD shader networks. This section documents the supported node types and how they map to USD primitives.

### MaterialX Node Definition IDs

When MaterialX is exported from applications like Blender, shader nodes use specific `info:id` values:

| Node Definition ID | Description | Used By |
|-------------------|-------------|---------|
| `ND_open_pbr_surface_surfaceshader` | OpenPBR Surface shader | Blender 4.5+ |
| `ND_standard_surface_surfaceshader` | Autodesk Standard Surface | Maya, older Blender |
| `ND_UsdPreviewSurface_surfaceshader` | USD Preview Surface | Universal fallback |
| `ND_image_color3` | Color image texture | Texture nodes |
| `ND_image_float` | Grayscale image texture | Roughness, metalness |
| `ND_texcoord_vector2` | UV coordinate generator | Texture coordinates |
| `ND_normalmap` | Normal map processor | Normal mapping |

### USD NodeGraph Structure

MaterialX node graphs in USD appear as `NodeGraph` prims containing shader nodes:

```usda
def NodeGraph "NG_materialx" {
    # Texture coordinate node
    def Shader "texcoord" {
        uniform token info:id = "ND_texcoord_vector2"
        int inputs:index = 0
        float2 outputs:out
    }

    # Image texture node
    def Shader "base_color_image" {
        uniform token info:id = "ND_image_color3"
        asset inputs:file = @textures/diffuse.png@
        string inputs:filtertype = "linear"
        float2 inputs:texcoord.connect = </Material/NG_materialx/texcoord.outputs:out>
        color3f outputs:out
    }

    # Output interface
    color3f outputs:base_color.connect = </Material/NG_materialx/base_color_image.outputs:out>
}
```

### Supported MaterialX Nodes (Three.js TSL) [W.I.P.]

> ⚠️ **Work in Progress**: Three.js TSL node graph processing is experimental and under active development. Not all nodes are fully tested.

The following MaterialX nodes are supported in the Three.js TSL (Three Shading Language) implementation:

#### Math Operations

| Node Type | MaterialX Name | Description | Inputs |
|-----------|---------------|-------------|--------|
| `add` | `ND_add_*` | Add two values | `in1`, `in2` |
| `subtract` | `ND_subtract_*` | Subtract values | `in1`, `in2` |
| `multiply` | `ND_multiply_*` | Multiply values | `in1`, `in2` |
| `divide` | `ND_divide_*` | Divide values | `in1`, `in2` |
| `power` | `ND_power_*` | Power function | `in1`, `in2` |
| `clamp` | `ND_clamp_*` | Clamp to range | `in`, `low`, `high` |
| `mix` | `ND_mix_*` | Linear interpolation | `bg`, `fg`, `mix` |
| `remap` | `ND_remap_*` | Remap value range | `in`, `inlow`, `inhigh`, `outlow`, `outhigh` |
| `smoothstep` | `ND_smoothstep_*` | Smooth interpolation | `low`, `high`, `in` |

#### Vector Operations

| Node Type | MaterialX Name | Description | Inputs |
|-----------|---------------|-------------|--------|
| `normalize` | `ND_normalize_*` | Normalize vector | `in` |
| `dotproduct` | `ND_dotproduct_*` | Dot product | `in1`, `in2` |
| `extract` | `ND_extract_*` | Extract component | `in`, `index` |
| `combine2` | `ND_combine2_*` | Combine to vec2 | `in1`, `in2` |
| `combine3` | `ND_combine3_*` | Combine to vec3 | `in1`, `in2`, `in3` |
| `combine4` | `ND_combine4_*` | Combine to vec4 | `in1`, `in2`, `in3`, `in4` |

#### Texture & Geometry

| Node Type | MaterialX Name | Description | Inputs |
|-----------|---------------|-------------|--------|
| `image` | `ND_image_*` | Sample texture | `file`, `texcoord` |
| `tiledimage` | `ND_tiledimage_*` | Tiled texture sample | `file`, `texcoord`, `uvtiling` |
| `texcoord` | `ND_texcoord_vector2` | UV coordinates | `index` |
| `position` | `ND_position_*` | World position | - |
| `normal` | `ND_normal_*` | World normal | - |
| `tangent` | `ND_tangent_*` | World tangent | - |

#### Color Operations

| Node Type | MaterialX Name | Description | Inputs |
|-----------|---------------|-------------|--------|
| `luminance` | `ND_luminance_*` | RGB to luminance | `in` |
| `constant` | `ND_constant_*` | Constant value | `value` |

#### Conditional

| Node Type | MaterialX Name | Description | Inputs |
|-----------|---------------|-------------|--------|
| `ifgreater` | `ND_ifgreater_*` | Conditional select | `value1`, `value2`, `in1`, `in2` |

### Node Connection Syntax

In USD, MaterialX node connections use the standard connection syntax:

```usda
# Connect texture coordinate to image node
float2 inputs:texcoord.connect = </Material/NodeGraph/texcoord.outputs:out>

# Connect image output to shader input
color3f inputs:base_color.connect = </Material/NodeGraph/diffuse_image.outputs:out>
```

### Primary UV Set Configuration

TinyUSDZ supports configuring the primary UV set name (similar to OpenUSD's `USDMTLX_PRIMARY_UV_NAME`):

```cpp
// C++ configuration
tinyusdz::MtlxConfig config;
config.primary_uv_name = "st";           // Default UV set name
config.secondary_uv_name_prefix = "st";  // Pattern for st1, st2, etc.
```

### Example: Complete MaterialX Material in USD

```usda
def Material "OpenPBRMaterial" {
    token outputs:surface.connect = </OpenPBRMaterial/OpenPBRShader.outputs:surface>

    def Shader "OpenPBRShader" {
        uniform token info:id = "ND_open_pbr_surface_surfaceshader"

        # Base layer with texture
        color3f inputs:base_color.connect = </OpenPBRMaterial/NodeGraph.outputs:base_color>
        float inputs:base_metalness = 0.0

        # Specular layer
        float inputs:specular_roughness.connect = </OpenPBRMaterial/NodeGraph.outputs:roughness>
        float inputs:specular_ior = 1.5

        # Normal map
        normal3f inputs:geometry_normal.connect = </OpenPBRMaterial/NodeGraph.outputs:normal>

        token outputs:surface
    }

    def NodeGraph "NodeGraph" {
        # UV coordinates
        def Shader "texcoord" {
            uniform token info:id = "ND_texcoord_vector2"
            int inputs:index = 0
            float2 outputs:out
        }

        # Base color texture
        def Shader "diffuse_tex" {
            uniform token info:id = "ND_image_color3"
            asset inputs:file = @textures/diffuse.png@
            string inputs:filtertype = "linear"
            float2 inputs:texcoord.connect = </OpenPBRMaterial/NodeGraph/texcoord.outputs:out>
            color3f outputs:out
        }

        # Roughness texture
        def Shader "roughness_tex" {
            uniform token info:id = "ND_image_float"
            asset inputs:file = @textures/roughness.png@
            float2 inputs:texcoord.connect = </OpenPBRMaterial/NodeGraph/texcoord.outputs:out>
            float outputs:out
        }

        # Normal map
        def Shader "normal_tex" {
            uniform token info:id = "ND_normalmap"
            asset inputs:file = @textures/normal.png@
            float2 inputs:texcoord.connect = </OpenPBRMaterial/NodeGraph/texcoord.outputs:out>
            normal3f outputs:out
        }

        # NodeGraph outputs
        color3f outputs:base_color.connect = </OpenPBRMaterial/NodeGraph/diffuse_tex.outputs:out>
        float outputs:roughness.connect = </OpenPBRMaterial/NodeGraph/roughness_tex.outputs:out>
        normal3f outputs:normal.connect = </OpenPBRMaterial/NodeGraph/normal_tex.outputs:out>
    }
}
```

### Supported Surface Shaders

TinyUSDZ supports the following MaterialX surface shader types:

| Shader Type | C++ Struct | info:id Value |
|-------------|-----------|---------------|
| OpenPBR Surface | `MtlxOpenPBRSurface` | `ND_open_pbr_surface_surfaceshader` |
| Standard Surface | `MtlxAutodeskStandardSurface` | `ND_standard_surface_surfaceshader` |
| USD Preview Surface | `MtlxUsdPreviewSurface` | `ND_UsdPreviewSurface_surfaceshader` |

### Light Shader Nodes (EDF)

MaterialX light shaders (Emission Distribution Functions) are also supported:

| Node Type | Description | Key Inputs |
|-----------|-------------|------------|
| `uniform_edf` | Uniform light emission | `color` |
| `conical_edf` | Conical/spot light emission | `color`, `inner_angle`, `outer_angle` |
| `measured_edf` | IES profile emission | `color`, `file` |
| `light` | Light shader wrapper | `edf`, `intensity` |

### Implementation Status

| Feature | C++ Core | JavaScript/Three.js |
|---------|----------|---------------------|
| NodeGraph parsing | ✅ Full | ✅ Basic |
| Surface shader conversion | ✅ OpenPBR, Standard, Preview | ✅ OpenPBR |
| Math nodes | ⚠️ Partial | ✅ Full |
| Texture nodes | ✅ image, tiledimage | ✅ image, tiledimage |
| Geometry nodes | ✅ texcoord, normal | ✅ texcoord, position, normal |
| Light nodes (EDF) | ✅ Full | ❌ Not implemented |

## Implementation Details

### Color Space Matrices

The color space conversions use standard transformation matrices derived from the CIE chromaticity coordinates of each color space:

- **sRGB/Rec.709**: Standard D65 white point, ITU-R BT.709 primaries
- **Rec.2020**: D65 white point, ITU-R BT.2020 primaries  
- **Display P3**: D65 white point, DCI-P3 primaries adapted to D65
- **ACEScg (AP1)**: D60 white point, ACES AP1 primaries
- **ACES 2065-1 (AP0)**: D60 white point, ACES AP0 primaries

### Transfer Functions

The library implements the following transfer functions:

1. **sRGB Transfer Function**: 
   - Forward: Piecewise function with linear segment below 0.04045
   - Inverse: Piecewise function with linear segment below 0.0031308

2. **Rec.709 Transfer Function**:
   - Similar to sRGB but with slightly different parameters
   - Linear segment below 0.018 (β = 0.018054 for 10-bit)

3. **Rec.2020 Transfer Function**:
   - Uses the same OETF as Rec.709 with 10-bit quantization parameters

4. **Simple Gamma Functions**:
   - Gamma 2.2: `y = x^2.2` (decode), `y = x^(1/2.2)` (encode)
   - Gamma 1.8: `y = x^1.8` (decode), `y = x^(1/1.8)` (encode)

### Performance Optimizations

- **Lookup Tables**: sRGB conversions use pre-computed 256-entry LUTs for 8-bit data
- **SIMD Support**: Vector operations are used where available
- **In-place Operations**: Memory efficient implementations where possible

## Common MaterialX Workflows

### Loading MaterialX Textures

When loading textures referenced in MaterialX documents:

1. Check the `colorspace` attribute on the texture node
2. Load the raw texture data
3. Convert from the specified color space to linear (working space)
4. Apply any additional MaterialX color transformations

### Example: Processing a MaterialX Surface

```cpp
// Typical MaterialX standard_surface material workflow
void ProcessMaterialXSurface(const MaterialXSurface& mtlxSurf) {
    // Base color is usually in srgb_texture space
    std::vector<float> baseColorLinear;
    if (mtlxSurf.baseColorSpace == "srgb_texture") {
        srgb_8bit_to_linear_f32(
            mtlxSurf.baseColorTexture,
            width, height, 3, 3,
            &baseColorLinear
        );
    }
    
    // Normal maps are typically "raw" (no color space)
    // Roughness, metallic are also usually "raw"
    // These don't need color space conversion
    
    // Emission might be in a different space
    if (mtlxSurf.emissionColorSpace == "acescg") {
        // Convert from ACEScg to working space if needed
        ACEScg_to_linear_sRGB(...);
    }
}
```

## File Locations

- **Header**: `src/image-util.hh` - Color conversion function declarations
- **Implementation**: `src/image-util.cc` - Color conversion implementations
- **Tydra Integration**: `src/tydra/render-data.{hh,cc}` - ColorSpace enum and inference
- **MaterialX Parser**: `sandbox/mtlx-parser/` - MaterialX document parsing

## Testing

Color space conversions can be tested using:
```bash
# Build with tests enabled
cmake -DTINYUSDZ_BUILD_TESTS=ON ..
make

# Run unit tests
./test_tinyusdz

# Test with MaterialX files
./tydra_to_renderscene data/materialx/StandardSurface/standard_surface_default.mtlx
```

## Current Implementation Status

### ✅ Completed Features

1. **Basic MaterialX XML Parsing**
   - XML parser in `src/usdMtlx.cc` using pugixml
   - Secure MaterialX parser in `sandbox/mtlx-parser/` (dependency-free)
   - Support for MaterialX v1.36, v1.37, v1.38, v1.39 (Blender 4.5+)

2. **Color Space Support**
   - Complete color space conversion functions in `src/image-util.cc`
   - Support for all MaterialX color spaces (sRGB, lin_rec709, ACEScg, etc.)
   - Color space inference in Tydra (`InferColorSpace()`)

3. **Shader Definitions**
   - `MtlxUsdPreviewSurface` shader struct defined
   - `MtlxAutodeskStandardSurface` shader struct (partial)
   - `OpenPBRSurface` shader struct with all parameters

4. **Tydra Material Conversion**
   - `UsdPreviewSurface` → `PreviewSurfaceShader` conversion
   - `OpenPBRSurface` → `OpenPBRSurfaceShader` conversion

5. **MaterialXConfigAPI Structure**
   - Basic `MaterialXConfigAPI` struct in `src/usdShade.hh`
   - `mtlx_version` attribute support

### ⚠️ Partial Implementation

1. **MaterialX File Import**
   - Basic `.mtlx` file loading via references
   - Limited node graph support
   - No full composition support

2. **Material Reconstruction**
   - `UsdPreviewSurface` reconstruction works
   - No `OpenPBRSurface` reconstruction in `prim-reconstruct.cc`
   - No `MtlxAutodeskStandardSurface` reconstruction

## Implementation Todo List

### 1. Core MaterialX Support

#### 1.1 MaterialXConfigAPI Implementation
- [ ] **Parse MaterialXConfigAPI from USD files**
  - [ ] Add MaterialXConfigAPI parsing in `prim-reconstruct.cc`
  - [ ] Support `config:mtlx:version` attribute
  - [ ] Support `config:mtlx:namespace` attribute
  - [ ] Support `config:mtlx:colorspace` attribute

- [ ] **Extend MaterialXConfigAPI structure**
  ```cpp
  struct MaterialXConfigAPI {
    TypedAttributeWithFallback<std::string> mtlx_version{"1.39"};  // Blender 4.5+ compatible
    TypedAttributeWithFallback<std::string> mtlx_namespace{""};
    TypedAttributeWithFallback<std::string> mtlx_colorspace{"lin_rec709"};
    TypedAttributeWithFallback<std::string> mtlx_sourceUri{""};
  };
  ```

#### 1.2 Shader Reconstruction
- [ ] **Implement OpenPBRSurface reconstruction**
  - [ ] Add `ReconstructShader<OpenPBRSurface>()` template specialization
  - [ ] Parse all OpenPBR parameters from USD properties
  - [ ] Handle texture connections for OpenPBR inputs

- [ ] **Implement MtlxAutodeskStandardSurface reconstruction**
  - [ ] Complete the StandardSurface struct with all parameters
  - [ ] Add `ReconstructShader<MtlxAutodeskStandardSurface>()`
  - [ ] Parse all StandardSurface parameters

- [ ] **Implement MtlxOpenPBRSurface reconstruction**
  - [ ] Add `MtlxOpenPBRSurface` struct (MaterialX-specific variant)
  - [ ] Add reconstruction support

#### 1.3 MaterialX Node Graph Support
- [ ] **Parse NodeGraph prims**
  - [ ] Implement `NodeGraph` struct in `usdShade.hh`
  - [ ] Add NodeGraph reconstruction in `prim-reconstruct.cc`
  - [ ] Support nested node connections

- [ ] **Node Types Support**
  - [ ] Image nodes (`<image>`, `<tiledimage>`)
  - [ ] Math nodes (`<add>`, `<multiply>`, etc.)
  - [ ] Color transform nodes
  - [ ] Procedural nodes (`<noise2d>`, `<fractal3d>`, etc.)

### 2. MaterialX File Loading

#### 2.1 Enhanced MaterialX Parser
- [ ] **Extend MaterialX DOM**
  - [ ] Parse `<nodedef>` definitions
  - [ ] Parse `<nodegraph>` structures
  - [ ] Parse `<material>` elements
  - [ ] Parse `<look>` and `<collection>` elements

- [ ] **MaterialX Version Handling**
  - [ ] Auto-detect MaterialX version from document
  - [ ] Version-specific attribute handling
  - [ ] Upgrade paths for older versions

#### 2.2 Asset Resolution
- [ ] **MaterialX File References**
  - [ ] Support `.mtlx` file references in USD
  - [ ] Implement MaterialX library path resolution
  - [ ] Cache loaded MaterialX documents

- [ ] **Include and Library Support**
  - [ ] Parse `<xi:include>` directives
  - [ ] Support MaterialX standard libraries
  - [ ] Custom library path configuration

### 3. Tydra Render Material Conversion

#### 3.1 Material Conversion Pipeline
- [ ] **MaterialX → RenderMaterial conversion**
  - [ ] Add `ConvertMaterialXShader()` method
  - [ ] Map MaterialX nodes to RenderMaterial properties
  - [ ] Handle node graph evaluation

- [ ] **Shader Network Evaluation**
  - [ ] Implement node connection resolver
  - [ ] Support value inheritance and defaults
  - [ ] Handle interface tokens and bindings

#### 3.2 Texture and Image Handling
- [ ] **MaterialX Texture Support**
  - [ ] Parse `<image>` node parameters
  - [ ] Support `<tiledimage>` with UV transforms
  - [ ] Handle texture color space attributes
  - [ ] Support UDIM and texture arrays

- [ ] **Color Space Conversions**
  - [ ] Auto-convert textures based on MaterialX colorspace
  - [ ] Support per-channel color spaces
  - [ ] Handle HDR textures correctly

### 4. Advanced MaterialX Features

#### 4.1 Geometry and Collections
- [ ] **Geometry Assignment**
  - [ ] Parse `<geominfo>` elements
  - [ ] Support geometry collections
  - [ ] Handle per-face material assignments

- [ ] **Material Variants**
  - [ ] Parse `<variant>` elements
  - [ ] Support variant sets
  - [ ] Implement variant selection API

#### 4.2 Units and Physical Properties
- [ ] **Unit System Support**
  - [ ] Parse unit attributes
  - [ ] Implement unit conversions
  - [ ] Support scene scale factors

- [ ] **Physical Material Properties**
  - [ ] IOR databases
  - [ ] Physical measurement units
  - [ ] Energy conservation validation

### 5. Testing and Validation

#### 5.1 Test Infrastructure
- [ ] **Unit Tests**
  - [ ] MaterialXConfigAPI parsing tests
  - [ ] Shader reconstruction tests
  - [ ] Node graph parsing tests
  - [ ] Color space conversion tests

- [ ] **Integration Tests**
  - [ ] Load MaterialX example files
  - [ ] Round-trip USD → MaterialX → USD
  - [ ] Validate against MaterialX test suite

#### 5.2 Example Files
- [ ] **Create test scenes**
  - [ ] Simple MaterialX material binding
  - [ ] Complex node graphs
  - [ ] Multi-material scenes
  - [ ] MaterialX library usage examples

### 6. Documentation

#### 6.1 API Documentation
- [ ] **Header Documentation**
  - [ ] Document MaterialXConfigAPI usage
  - [ ] Document MaterialX shader types
  - [ ] Document conversion functions

- [ ] **Usage Examples**
  - [ ] Loading MaterialX files
  - [ ] Creating MaterialX materials programmatically
  - [ ] Converting MaterialX to render materials

#### 6.2 User Guide
- [ ] **MaterialX Integration Guide**
  - [ ] How to use MaterialX in USD files
  - [ ] Best practices for MaterialX materials
  - [ ] Performance considerations

## Implementation Priority

### Phase 1 (High Priority)
1. MaterialXConfigAPI parsing and reconstruction
2. OpenPBRSurface reconstruction
3. Basic NodeGraph support
4. MaterialX file reference resolution

### Phase 2 (Medium Priority)
1. Complete StandardSurface support
2. Enhanced node graph evaluation
3. Texture and image node support
4. Color space auto-conversion

### Phase 3 (Low Priority)
1. Geometry assignments and collections
2. Material variants
3. Unit system support
4. Advanced procedural nodes

## Code Locations

### Files to Modify

1. **`src/prim-reconstruct.cc`**
   - Add MaterialXConfigAPI reconstruction
   - Add OpenPBRSurface shader reconstruction
   - Add NodeGraph prim support

2. **`src/usdShade.hh`**
   - Extend MaterialXConfigAPI struct
   - Add NodeGraph struct
   - Complete shader definitions

3. **`src/usdMtlx.cc`**
   - Enhance MaterialX parsing
   - Add node graph support
   - Implement material conversion

4. **`src/tydra/render-data.cc`**
   - Add MaterialX shader conversion
   - Implement node evaluation
   - Handle texture references

5. **`src/composition.cc`**
   - Add MaterialX file reference support
   - Implement MaterialX composition rules

### New Files to Create

1. **`src/materialx-eval.hh/cc`**
   - Node graph evaluation engine
   - Connection resolver
   - Value computation

2. **`tests/test-materialx.cc`**
   - Comprehensive MaterialX tests
   - Validation suite

3. **`examples/materialx-viewer/`**
   - Example viewer for MaterialX materials
   - Demonstration of features

## Dependencies and Requirements

### External Dependencies
- None (maintain dependency-free approach)
- Optional: MaterialX validator for testing

### Build Configuration
- Add `TINYUSDZ_WITH_FULL_MATERIALX` option
- Enable by default when `TINYUSDZ_WITH_USDMTLX=ON`

## Performance Considerations

1. **Memory Management**
   - Cache parsed MaterialX documents
   - Lazy evaluation of node graphs
   - Efficient texture loading

2. **Optimization Opportunities**
   - Pre-compile node graphs to bytecode
   - SIMD color space conversions
   - Parallel node evaluation

## Compatibility Notes

1. **USD Compatibility**
   - Follow USD MaterialX schema conventions
   - Support Pixar's MaterialX integration patterns
   - Maintain compatibility with pxrUSD

2. **MaterialX Version Support**
   - Primary: MaterialX 1.39 (current - Blender 4.5+ compatible)
   - Legacy: MaterialX 1.36, 1.37, 1.38
   - Future: MaterialX 1.40+ preparation

## Validation Checklist

- [ ] All MaterialX example files load correctly
- [ ] Color spaces are properly converted
- [ ] Node graphs evaluate correctly
- [ ] Textures are loaded with correct parameters
- [ ] Round-trip preservation of MaterialX data
- [ ] Performance meets requirements
- [ ] Memory usage is bounded
- [ ] Security: no buffer overflows or memory leaks

## Related Documentation

- **[OpenPBR Parameters Reference](./openpbr-parameters-reference.md)** - Comprehensive parameter mapping guide
  - Complete list of all 38 OpenPBR parameters
  - Blender MaterialX export parameter names
  - Three.js MeshPhysicalMaterial support status
  - Conversion recommendations and limitations

## References

### MaterialX & OpenPBR
- [MaterialX Specification v1.38](https://www.materialx.org/docs/api/MaterialX_v1_38_Spec.pdf)
- [MaterialX GitHub Repository](https://github.com/AcademySoftwareFoundation/MaterialX)
- [OpenPBR Specification](https://academysoftwarefoundation.github.io/OpenPBR/)
- [OpenPBR GitHub Repository](https://github.com/AcademySoftwareFoundation/OpenPBR)

### USD Integration
- [USD MaterialX Schema](https://openusd.org/release/api/usd_mtlx_page.html)
- [PBR Material Interoperability (MaterialX, USD, glTF)](https://metaverse-standards.org/wp-content/uploads/PBR-material-interoperability.pdf)

### Blender Documentation
- [Blender 4.5 LTS Release Notes - Pipeline & I/O](https://developer.blender.org/docs/release_notes/4.5/pipeline_assets_io/)
- [Principled BSDF - Blender 4.5 Manual](https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/principled.html)
- [Blender Principled BSDF v2 Development](https://projects.blender.org/blender/blender/issues/99447)
- [Blender MaterialX Export Implementation](https://projects.blender.org/blender/blender/pulls/138165)

### Color Space Standards
- [ITU-R BT.709](https://www.itu.int/rec/R-REC-BT.709)
- [ITU-R BT.2020](https://www.itu.int/rec/R-REC-BT.2020)
- [ACES Documentation](https://www.oscars.org/science-technology/sci-tech-projects/aces)
- [sRGB Specification](https://www.w3.org/Graphics/Color/sRGB)

## Notes

- MaterialX support is critical for modern production pipelines
- Prioritize compatibility with major DCC tools (Maya, Houdini, Blender)
- Consider future integration with MaterialX code generation
- Maintain security-first approach in all implementations