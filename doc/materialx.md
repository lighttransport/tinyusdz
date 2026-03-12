# MaterialX Support in TinyUSDZ

TinyUSDZ provides MaterialX integration including parsing `.mtlx` files, color space conversions, OpenPBR/StandardSurface shader support, and a JavaScript MaterialX pipeline for Three.js.

## Source Files

| File | Description |
|------|-------------|
| `src/usdShade.hh` | MaterialXConfigAPI, OpenPBRSurface, Material structs |
| `src/usdMtlx.hh` | MtlxOpenPBRSurface, MtlxAutodeskStandardSurface defs |
| `src/color-space.hh` | ColorSpace enum and utility functions |
| `src/image-util.{hh,cc}` | Color space conversion functions |
| `src/mtlx-dom.{hh,cc}` | MaterialX document object model |
| `src/mtlx-xml-parser.{hh,cc}` | MaterialX XML parser |
| `src/mtlx-simple-parser.{hh,cc}` | Simplified MaterialX parser |
| `src/mtlx-usd-adapter.hh` | USD-MaterialX integration |
| `src/tydra/render-data.{hh,cc}` | OpenPBRSurfaceShader, RenderMaterial conversion |
| `src/tydra/materialx-to-json.{hh,cc}` | MaterialX to JSON conversion |
| `web/js/src/tinyusdz/TinyUSDZMaterialX.js` | JS OpenPBR to Three.js conversion |

## Supported Shader Types

| Shader | C++ Struct | `info:id` |
|--------|-----------|-----------|
| OpenPBR Surface | `OpenPBRSurface` / `MtlxOpenPBRSurface` | `ND_open_pbr_surface_surfaceshader` |
| Standard Surface | `MtlxAutodeskStandardSurface` | `ND_standard_surface_surfaceshader` |
| USD Preview Surface | `UsdPreviewSurface` / `MtlxUsdPreviewSurface` | `ND_UsdPreviewSurface_surfaceshader` |

### MaterialXConfigAPI

```cpp
struct MaterialXConfigAPI {
    TypedAttributeWithFallback<std::string> mtlx_version{"1.38"};
    TypedAttributeWithFallback<std::string> mtlx_namespace{""};
    TypedAttributeWithFallback<std::string> mtlx_colorspace{""};
    TypedAttributeWithFallback<std::string> mtlx_sourceUri{""};
};
```

Applied to `Material` prims via `config:mtlx:version`, `config:mtlx:colorspace`, etc.

---

## Color Space Support

### ColorSpace Enum (`src/color-space.hh`)

| Token | Enum | Description |
|-------|------|-------------|
| `lin_rec709_scene` | `LinRec709Scene` | Linear Rec.709/sRGB (default) |
| `lin_ap0_scene` | `LinAp0Scene` | ACES 2065-1 (AP0) |
| `lin_ap1_scene` | `LinAp1Scene` | ACES CG (AP1) |
| `lin_p3d65_scene` | `LinP3D65Scene` | Linear P3-D65 |
| `lin_rec2020_scene` | `LinRec2020Scene` | Linear Rec.2020 |
| `lin_adobergb_scene` | `LinAdobeRGBScene` | Linear Adobe RGB |
| `lin_ciexyzd65_scene` | `LinCieXyzD65Scene` | CIE XYZ-D65 |
| `srgb_rec709_scene` | `SrgbRec709Scene` | sRGB Rec.709 |
| `srgb_ap1_scene` | `SrgbAp1Scene` | sRGB AP1 |
| `srgb_p3d65_scene` | `SrgbP3D65Scene` | sRGB P3-D65 |
| `g22_rec709_scene` | `G22Rec709Scene` | Gamma 2.2 Rec.709 |
| `g22_ap1_scene` | `G22Ap1Scene` | Gamma 2.2 AP1 |
| `g22_adobergb_scene` | `G22AdobeRGBScene` | Gamma 2.2 Adobe RGB |
| `g18_rec709_scene` | `G18Rec709Scene` | Gamma 1.8 Rec.709 |
| `data` | `Data` | Non-color data (normals, displacement) |
| `raw` | *(alias)* | Legacy alias for `data` |
| `unknown` | `Unknown` | Unspecified |
| `identity` | *(alias)* | Legacy alias for `unknown` |

Utility functions: `to_token()`, `from_token()`, `is_linear()`, `is_data()`.

### Conversion Functions (`src/image-util.{hh,cc}`)

```cpp
// sRGB
bool srgb_8bit_to_linear_f32(const std::vector<uint8_t> &in, ...);
bool srgb_f32_to_linear_f32(const std::vector<float> &in, ...);

// Rec.2020
bool linear_rec2020_to_linear_sRGB(const std::vector<float> &in, ...);
bool linear_sRGB_to_linear_rec2020(const std::vector<float> &in, ...);

// ACEScg (AP1)
bool linear_sRGB_to_ACEScg(const std::vector<float> &in, ...);
bool ACEScg_to_linear_sRGB(const std::vector<float> &in, ...);

// Display P3
bool linear_displayp3_to_linear_sRGB(const std::vector<float> &in, ...);
bool linear_sRGB_to_linear_displayp3(const std::vector<float> &in, ...);

// Gamma
bool gamma22_f32_to_linear_f32(const std::vector<float> &in, ...);
bool gamma18_f32_to_linear_f32(const std::vector<float> &in, ...);
```

Performance: sRGB 8-bit conversions use pre-computed 256-entry LUTs.

---

## Tydra MaterialX Conversion Pipeline

### Conversion Flow

```
USD Stage -> Material with shaders -> ConvertMaterial() -> RenderMaterial
  ├── OpenPBRSurface         -> ConvertOpenPBRSurfaceShader()   -> RenderMaterial.openPBRShader
  ├── MtlxOpenPBRSurface     -> (convert to OpenPBRSurface)     -> same path
  ├── MtlxStandardSurface    -> ConvertMtlxStdSurfToOpenPBR()   -> same path
  └── UsdPreviewSurface      -> ConvertPreviewSurfaceShader()   -> RenderMaterial.surfaceShader
```

StandardSurface -> OpenPBR conversion handles key differences:
- `coat_affect_color`: float -> color3f (broadcast scalar)
- `opacity`: color3f -> float (luminance extraction)
- `normal`/`tangent`: TypedAttribute -> TypedAttributeWithFallback

### OpenPBR Surface Fields

| Category | Fields |
|----------|--------|
| Base | `base_weight`, `base_color`, `base_roughness`, `base_metalness`, `base_diffuse_roughness` |
| Specular | `specular_weight`, `specular_color`, `specular_roughness`, `specular_ior`, `specular_ior_level`, `specular_anisotropy`, `specular_rotation`, `specular_roughness_anisotropy` |
| Transmission | `transmission_weight`, `transmission_color`, `transmission_depth`, `transmission_scatter`, `transmission_scatter_anisotropy`, `transmission_dispersion_abbe_number`, `transmission_dispersion_scale` |
| Subsurface | `subsurface_weight`, `subsurface_color`, `subsurface_radius`, `subsurface_radius_scale`, `subsurface_scale`, `subsurface_anisotropy`, `subsurface_scatter_anisotropy` |
| Coat | `coat_weight`, `coat_color`, `coat_roughness`, `coat_anisotropy`, `coat_rotation`, `coat_ior`, `coat_affect_color`, `coat_affect_roughness`, `coat_roughness_anisotropy`, `coat_darkening` |
| Sheen/Fuzz | `sheen_weight`, `sheen_color`, `sheen_roughness`, `fuzz_weight`, `fuzz_color`, `fuzz_roughness` |
| Thin Film | `thin_film_weight`, `thin_film_thickness`, `thin_film_ior` |
| Emission | `emission_luminance`, `emission_color` |
| Geometry | `opacity`, `normal`, `tangent`, `coat_normal`, `coat_tangent` |

### Material Tag Classification

| Shader | Opaque | Translucent | Masked |
|--------|--------|-------------|--------|
| OpenPBR | Default | `transmission_weight > 0` or `opacity < 1` | -- |
| UsdPreviewSurface | Default | `opacity < 1` | `opacityThreshold > 0` |

### NodeGraph Traversal (`ExtractMtlxNodeGraphInfo`)

Follows `inputs:in` connections through node chains (max depth 15):

| Node Type | Action | Extracted Info |
|-----------|--------|----------------|
| `ND_normalmap*` | Follow `inputs:in` | `normal_map_scale`, `has_normal_map` |
| `ND_rotate3d_vector3` | Follow `inputs:in` | `tangent_rotation` |
| `ND_image_*` | Terminal | `normal_map_texture` |
| `ND_tiledimage_*` | Terminal | texture path, `uvtiling`, `uvoffset` |
| `ND_texcoord_*` | Terminal | `texcoord_index` |
| `ND_geompropvalue_*` | Terminal | `geomprop_name` (primvar) |
| `ND_separate*` / `ND_extract_*` | Follow `inputs:in` | Multi-output / channel extraction |
| `ND_convert_*` | Follow `inputs:in` | Type conversion passthrough |
| `ND_constant_*` | Terminal | Constant value |
| Math/color ops | Follow `inputs:in` | Passthrough traversal |

### Texture Colorspace Handling

| Parameter Type | sourceColorSpace |
|----------------|-----------------|
| Color params (`base_color`, `emission_color`, `specular_color`, `coat_color`, `sheen_color`, `subsurface_color`, `transmission_color`, `fuzz_color`) | `sRGB` |
| Non-color params (roughness, metalness, normal, weight, IOR, etc.) | `Raw` |

### Texture Wrap Modes

| MaterialX | USD `UsdUVTexture::Wrap` |
|-----------|-------------------------|
| `periodic` | `Repeat` |
| `clamp` | `Clamp` |
| `mirror` | `Mirror` |
| `constant` | `Black` |

### Mesh Attribute Resolution (UVs and Normals)

**UVs**: Tydra checks `ListUVNames(material)` for shader-referenced UV sets, falls back to `primvars:st` (configurable via `MeshConverterConfig::default_texcoords_primvar_name`). This mirrors OpenUSD's `defaultgeomprop="UV0"` -> `primvars:st` mapping.

**Normals**: Loaded unconditionally from mesh geometry:
1. `primvars:normals` (primvar)
2. `normals` (legacy attribute)
3. Auto-compute smooth normals

---

## Blender MaterialX Export

### Principled BSDF to OpenPBR Surface Mapping

Blender 4.5+ exports Principled BSDF as OpenPBR Surface (`ND_open_pbr_surface_surfaceshader`). Both MaterialX and UsdPreviewSurface are exported on the same Material.

#### Base / Specular

| Blender Principled BSDF | OpenPBR Surface | Notes |
|------------------------|-----------------|-------|
| Base Color | `base_color` | Direct |
| Metallic | `base_metalness` | Direct |
| Diffuse Roughness | `base_diffuse_roughness` | Oren-Nayar (0 = Lambertian) |
| Roughness | `specular_roughness` | Direct |
| IOR | `specular_ior` | Direct |
| IOR Level | `specular_weight` | **Multiply by 2.0** (Blender 0.5 = neutral, OpenPBR 1.0 = neutral) |
| Specular Tint | `specular_color` | Direct |
| Anisotropic | `specular_roughness_anisotropy` | Direct |
| Anisotropic Rotation | *(tangent vector)* | Tangent rotated around normal |

#### Subsurface / Transmission

| Blender | OpenPBR | Notes |
|---------|---------|-------|
| Subsurface Weight | `subsurface_weight` | Direct |
| Subsurface Scale | `subsurface_radius` | Mean free path |
| Subsurface Radius | `subsurface_radius_scale` | Per-channel RGB multiplier |
| Subsurface Anisotropy | `subsurface_scatter_anisotropy` | Direct |
| Transmission Weight | `transmission_weight` | Direct |

#### Coat / Sheen / Thin Film / Emission

| Blender | OpenPBR | Notes |
|---------|---------|-------|
| Coat Weight | `coat_weight` | Direct |
| Coat Tint | `coat_color` | Direct |
| Coat Roughness | `coat_roughness` | Direct |
| Coat IOR | `coat_ior` | Direct |
| Coat Normal | `geometry_coat_normal` | Direct |
| Sheen Weight | `fuzz_weight` | **Renamed**: sheen -> fuzz |
| Sheen Tint | `fuzz_color` | **Renamed** |
| Sheen Roughness | `fuzz_roughness` | Direct |
| Thin Film Thickness | `thin_film_thickness` | Direct |
| Thin Film IOR | `thin_film_ior` | Direct |
| Emission Color | `emission_color` | Direct |
| Emission Strength | `emission_luminance` | Direct |
| Alpha | `geometry_opacity` | Direct |
| Normal | `geometry_normal` | Direct |

### Blender Node to MaterialX Mapping

Blender shader nodes translate to MaterialX standard library nodes. See `doc/blender_to_materialx_node_mapping.json` for the complete machine-readable mapping. Key patterns:

**Color Nodes:**

| Blender Node | MaterialX Translation |
|-------------|----------------------|
| Invert | `ND_subtract_color3` (1-color) + `ND_mix_color3` (Fac blend) |
| Hue/Saturation/Value | `ND_combine3_vector3` + `ND_hsvadjust_color3` + `ND_mix_color3` |
| Brightness/Contrast | `ND_multiply` + `ND_add` + `ND_subtract` + `ND_max` chain |
| Gamma | `ND_power_color3` |
| RGB to BW | `ND_luminance_color3` + `ND_extract_color3` |
| Mix | `ND_mix_color3` |
| Separate Color | `ND_extract_color3` (per channel) |
| Combine Color | `ND_combine3_color3` |

**Math/Vector Nodes:**

| Blender Operation | MaterialX Node |
|-------------------|----------------|
| ADD/SUBTRACT/MULTIPLY/DIVIDE | `ND_{op}_float` or `ND_{op}_vector3` |
| DOT_PRODUCT | `ND_dotproduct_vector3` |
| NORMALIZE | `ND_normalize_vector3` |
| CLAMP | `ND_clamp_float` |
| MAP_RANGE | `ND_remap_float` |
| POWER/SQRT/ABS | `ND_power_float` / `ND_sqrt_float` / `ND_absval_float` |
| Trig functions | `ND_sin_float` / `ND_cos_float` / `ND_tan_float` |

**Geometry (auto-generated):**

```
ND_normal_vector3(world) -> ND_normalize_vector3 -> Normal
ND_tangent_vector3(world) -> ND_normalize_vector3 -> ND_rotate3d_vector3(-90) -> Tangent
```

**Limitations:**
- RGB Curves, ColorRamp: only pre-computed with constant inputs
- OSL scripts, custom groups: not supported
- MixRGB blend modes: only `MIX` fully translated
- Noise/Voronoi: approximations via `ND_noise3d_float` / `ND_cellnoise3d_float`

### Blender Export Options

```python
bpy.ops.wm.usd_export(
    filepath="output.usda",
    export_materials=True,
    generate_materialx_network=True,
    export_textures=True,
)
```

Test files: `tests/feat/node-mtlx/*.usda`

---

## Three.js / WebGL Integration

### Material Implementations

| Implementation | Class | Use Case |
|---------------|-------|----------|
| MeshPhysicalMaterial | `THREE.MeshPhysicalMaterial` | Standard PBR, broad compatibility |
| OpenPBRMaterial | Custom `ShaderMaterial` | Full OpenPBR BRDF (Oren-Nayar, coat IOR, fuzz) |

### OpenPBR to MeshPhysicalMaterial Mapping

| OpenPBR | MeshPhysicalMaterial | Notes |
|---------|---------------------|-------|
| `base_color` | `color` / `map` | Diffuse color |
| `base_metalness` | `metalness` / `metalnessMap` | 0-1 |
| `specular_roughness` | `roughness` / `roughnessMap` | 0-1 |
| `specular_ior` | `ior` | Index of refraction |
| `transmission_weight` | `transmission` | 0-1 |
| `coat_weight` | `clearcoat` | 0-1 |
| `coat_roughness` | `clearcoatRoughness` | 0-1 |
| `fuzz_weight` / `sheen_weight` | `sheen` | Approximated |
| `thin_film_weight` | `iridescence` | 0-1 |
| `emission_color` | `emissive` / `emissiveMap` | RGB |
| `emission_luminance` | `emissiveIntensity` | Nits |
| `geometry_opacity` | `opacity` | 0-1 |
| `geometry_normal` | `normalMap` | Tangent space |

### API

```javascript
import {
    convertOpenPBRToMeshPhysicalMaterial,      // Immediate (textures load async)
    convertOpenPBRToMeshPhysicalMaterialLoaded  // Await all textures
} from 'tinyusdz/TinyUSDZMaterialX.js';

const material = await convertOpenPBRToMeshPhysicalMaterialLoaded(matData, usdScene, options);
```

HDR/EXR textures supported via TinyUSDZ WASM decoder (HDR) and Three.js EXRLoader.

### NodeGraph Optimizer

Location: `web/js/src/tinyusdz/TinyUSDZMaterialX.js`

Simplifies MaterialX node graphs from Blender export:

```javascript
import { optimizeNodeGraph, NodeGraphOptimizationLevel } from 'tinyusdz/TinyUSDZMaterialX.js';
const optimized = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
```

**Optimization Levels:** NONE (0), BASIC (1, identity removal), STANDARD (2, patterns + identity), AGGRESSIVE (3, + constant folding)

**Pattern Categories:**
- **Blender-specific**: Invert (subtract+mix -> invert), Brightness/Contrast, HSV adjust
- **Channel ops**: Swizzle detection, separate/combine passthrough, single channel modification
- **Math**: Add/subtract inverse cancellation, multiply/divide inverse, idempotent chains
- **Conversion**: Type roundtrips (color3<->vector3), colorspace roundtrips, chained normalize
- **Identity removal**: multiply by 1, add 0, mix factor=0/1, etc.

---

## pxrUSD MaterialX Parser Reference

Reference for pxrUSD's `pxr/usd/usdMtlx/parser.cpp` which converts MaterialX node definitions to USD Sdr shader nodes.

### Key Components

1. **ShaderBuilder**: Accumulates data for `SdrShaderNode` construction (properties, metadata, name remapping)
2. **AddProperty()**: Maps MaterialX typed elements to `SdrShaderProperty` with type resolution via `UsdMtlxGetUsdType()`, default values via `UsdMtlxGetUsdValue()`, UI metadata, colorspace, and primvar tracking
3. **ParseElement()**: Main NodeDef parsing - determines context (shader/pattern), collects primvars from geometry nodes, iterates inputs/outputs
4. **UsdMtlxParserPlugin**: Loads MaterialX documents, looks up NodeDef by identifier, returns `SdrShaderNode`

### Environment Settings

`USDMTLX_PRIMARY_UV_NAME`: Override primary UV set name (default: `UsdUtilsGetPrimaryUVSetName()` -> `"st"`)

---

## USD NodeGraph Structure

```usda
def Material "OpenPBRMaterial" {
    token outputs:surface.connect = </OpenPBRMaterial/Shader.outputs:surface>

    def Shader "Shader" {
        uniform token info:id = "ND_open_pbr_surface_surfaceshader"
        color3f inputs:base_color.connect = </OpenPBRMaterial/NG.outputs:base_color>
        float inputs:specular_roughness = 0.5
        token outputs:surface
    }

    def NodeGraph "NG" {
        def Shader "texcoord" {
            uniform token info:id = "ND_texcoord_vector2"
            int inputs:index = 0
            float2 outputs:out
        }
        def Shader "diffuse_tex" {
            uniform token info:id = "ND_image_color3"
            asset inputs:file = @textures/diffuse.png@
            float2 inputs:texcoord.connect = </OpenPBRMaterial/NG/texcoord.outputs:out>
            color3f outputs:out
        }
        color3f outputs:base_color.connect = </OpenPBRMaterial/NG/diffuse_tex.outputs:out>
    }
}
```

---

## Implementation Status

### Completed

- MaterialX XML parsing (v1.36-1.39)
- Color space conversions (all MaterialX spaces)
- MaterialXConfigAPI struct and version attribute
- OpenPBRSurface, StandardSurface, UsdPreviewSurface shader structs
- Tydra: OpenPBR -> OpenPBRSurfaceShader conversion
- Tydra: StandardSurface -> OpenPBR -> conversion
- Tydra: NodeGraph traversal with texture/normal/tangent extraction
- JS: OpenPBR to MeshPhysicalMaterial / OpenPBRMaterial conversion
- JS: NodeGraph optimizer

### Partial / In Progress

- MaterialX file import via references (basic, no full composition)
- OpenPBRSurface reconstruction in `prim-reconstruct.cc` (not yet implemented)
- MtlxAutodeskStandardSurface reconstruction
- Displacement/volume shader evaluation (connections tracked, not evaluated)

### Not Yet Implemented

- NodeGraph prim reconstruction
- Procedural nodes (`noise2d`, `fractal3d`)
- MaterialX `<xi:include>` / library resolution
- Geometry assignments and collections
- Unit system support

## References

- [MaterialX Specification](https://www.materialx.org/docs/api/MaterialX_v1_38_Spec.pdf)
- [OpenPBR Specification](https://academysoftwarefoundation.github.io/OpenPBR/)
- [USD MaterialX Schema](https://openusd.org/release/api/usd_mtlx_page.html)
- [Blender USD Export](https://docs.blender.org/manual/en/latest/files/import_export/usd.html)
- [PBR Material Interoperability](https://metaverse-standards.org/wp-content/uploads/PBR-material-interoperability.pdf)
