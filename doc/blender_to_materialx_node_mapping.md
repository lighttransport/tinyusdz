# Blender to MaterialX Node Mapping

This document describes how Blender shader nodes are translated to USD MaterialX when using Blender's USD exporter with `generate_materialx_network=True`.

## Overview

Blender 5.0 exports materials using the **OpenPBR Surface** shader (`ND_open_pbr_surface_surfaceshader`) as the primary MaterialX surface shader. Intermediate nodes in the node graph are translated to MaterialX standard library nodes.

**Important Notes:**
- Constant-only node graphs may be **pre-computed** at export time (result baked into a single value)
- Some nodes require **linked inputs** to generate MaterialX node graphs
- Geometry nodes (normal, tangent) are always exported for proper shading

## Color Nodes

### Invert (`ShaderNodeInvert`)

**Blender Node:**
- Input: `Color` (RGBA), `Fac` (float)
- Output: `Color` (RGBA)

**MaterialX Translation:**
```
┌─────────────────────┐     ┌──────────────────────┐     ┌─────────────────┐
│ ND_constant_color3  │────▶│ ND_subtract_color3   │────▶│ ND_mix_color3   │
│ (input color)       │     │ in1=(1,1,1)          │     │ bg=input        │
└─────────────────────┘     │ in2=input            │     │ fg=subtracted   │
                            └──────────────────────┘     │ mix=Fac         │
                                                         └─────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_constant_color3` | Input color value |
| `ND_subtract_color3` | Compute `(1,1,1) - color` (inverted) |
| `ND_mix_color3` | Blend original and inverted by Fac |

---

### Hue/Saturation/Value (`ShaderNodeHueSaturation`)

**Blender Node:**
- Inputs: `Hue`, `Saturation`, `Value`, `Fac`, `Color`
- Output: `Color`

**MaterialX Translation:**
```
┌─────────────────────┐     ┌──────────────────────┐     ┌─────────────────┐
│ ND_combine3_vector3 │────▶│ ND_hsvadjust_color3  │────▶│ ND_mix_color3   │
│ (hue, sat, value)   │     │ amount=hsv_vector    │     │ mix=Fac         │
└─────────────────────┘     │ in=input_color       │     └─────────────────┘
                            └──────────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_combine3_vector3` | Combine H, S, V into vector3 |
| `ND_hsvadjust_color3` | Apply HSV adjustment |
| `ND_mix_color3` | Blend with Fac |

---

### Brightness/Contrast (`ShaderNodeBrightContrast`)

**Blender Node:**
- Inputs: `Color`, `Bright`, `Contrast`
- Output: `Color`

**MaterialX Translation:**
```
Input ──▶ ND_multiply_color3 ──▶ ND_add_color3 ──▶ ND_subtract_color3 ──▶ ND_max_color3 ──▶ Output
          (contrast scale)       (brightness)      (offset)               (clamp >= 0)
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_multiply_color3` | Apply contrast scaling |
| `ND_add_color3` | Add brightness offset |
| `ND_subtract_color3` | Adjust contrast center point |
| `ND_max_color3` | Clamp result to non-negative |

---

### Gamma (`ShaderNodeGamma`)

**Blender Node:**
- Inputs: `Color`, `Gamma`
- Output: `Color`

**MaterialX Translation:**
```
┌─────────────────────┐     ┌──────────────────────┐
│ ND_constant_color3  │────▶│ ND_power_color3      │────▶ Output
│ (input color)       │     │ in1=input            │
└─────────────────────┘     │ in2=(gamma,gamma,    │
                            │      gamma)          │
                            └──────────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_power_color3` | Apply gamma correction: `color ^ gamma` |

---

### RGB to BW (`ShaderNodeRGBToBW`)

**Blender Node:**
- Input: `Color`
- Output: `Val` (float)

**MaterialX Translation:**
```
┌─────────────────────┐     ┌──────────────────────┐     ┌─────────────────┐
│ ND_constant_color3  │────▶│ ND_luminance_color3  │────▶│ ND_extract_color3│
│ (input color)       │     │ (compute luminance)  │     │ index=0         │
└─────────────────────┘     └──────────────────────┘     └─────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_luminance_color3` | Convert RGB to luminance |
| `ND_extract_color3` | Extract single channel |

---

### Mix RGB (`ShaderNodeMixRGB`) / Mix (`ShaderNodeMix`)

**Blender Node:**
- Inputs: `Fac`, `Color1`/`A`, `Color2`/`B`
- Output: `Color`/`Result`

**MaterialX Translation:**
```
┌─────────────────────┐
│ ND_mix_color3       │
│ bg=Color1           │
│ fg=Color2           │────▶ Output
│ mix=Fac             │
└─────────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_mix_color3` | Linear interpolation between colors |

**Note:** With constant inputs, Blender may pre-compute the result.

---

### Separate Color (`ShaderNodeSeparateColor`)

**Blender Node:**
- Input: `Color`
- Outputs: `Red`, `Green`, `Blue` (or `Hue`, `Saturation`, `Value` depending on mode)

**MaterialX Translation (RGB mode):**
```
┌─────────────────────┐     ┌──────────────────────┐
│ ND_constant_color3  │────▶│ ND_extract_color3    │────▶ Red (index=0)
│ (input color)       │     │ ND_extract_color3    │────▶ Green (index=1)
└─────────────────────┘     │ ND_extract_color3    │────▶ Blue (index=2)
                            └──────────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_extract_color3` | Extract single channel by index (0=R, 1=G, 2=B) |

---

### Combine Color (`ShaderNodeCombineColor`)

**Blender Node:**
- Inputs: `Red`, `Green`, `Blue`
- Output: `Color`

**MaterialX Translation:**
```
R ──┐
G ──┼──▶ ND_combine3_color3 ──▶ Output
B ──┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_combine3_color3` | Combine 3 floats into color3 |

---

### RGB Curves (`ShaderNodeRGBCurve`)

**MaterialX Translation:**

RGB Curves with constant inputs are **pre-computed** at export time. The curve function is evaluated and the result is stored as a constant color.

| Behavior | Description |
|----------|-------------|
| Constant input | Pre-computed, exported as `ND_constant_color3` |
| Dynamic input | May require custom implementation |

---

### ColorRamp (`ShaderNodeValToRGB`)

**MaterialX Translation:**

ColorRamp nodes are typically **pre-computed** at export time when the input factor is constant. For dynamic inputs, the gradient may be sampled and baked.

| Behavior | Description |
|----------|-------------|
| Constant input | Pre-computed gradient lookup |
| Dynamic input | Limited support |

---

## Vector/Math Nodes

### Separate XYZ (`ShaderNodeSeparateXYZ`)

**Blender Node:**
- Input: `Vector`
- Outputs: `X`, `Y`, `Z`

**MaterialX Translation:**
```
┌─────────────────────────┐     ┌──────────────────────┐
│ ND_convert_color3_vector3│───▶│ ND_extract_vector3   │──▶ X (index=0)
│ (if input is color)     │     │ ND_extract_vector3   │──▶ Y (index=1)
└─────────────────────────┘     │ ND_extract_vector3   │──▶ Z (index=2)
                                └──────────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_convert_color3_vector3` | Convert color to vector if needed |
| `ND_extract_vector3` | Extract component by index |

---

### Combine XYZ (`ShaderNodeCombineXYZ`)

**Blender Node:**
- Inputs: `X`, `Y`, `Z`
- Output: `Vector`

**MaterialX Translation:**
```
X ──┐
Y ──┼──▶ ND_combine3_vector3 ──▶ Output
Z ──┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_combine3_vector3` | Combine 3 floats into vector3 |

---

### Vector Math (`ShaderNodeVectorMath`)

**Blender Node:**
- Inputs: `Vector` (multiple)
- Output: `Vector` or `Value`

**MaterialX Translation (ADD operation):**
```
┌─────────────────────────┐     ┌──────────────────────┐     ┌─────────────────────────┐
│ ND_convert_color3_vector3│───▶│ ND_add_vector3       │───▶│ ND_convert_vector3_color3│
│ (input A)               │     │ in1=A, in2=B         │     │ (output conversion)     │
└─────────────────────────┘     └──────────────────────┘     └─────────────────────────┘
```

**Operation Mappings:**

| Blender Operation | MaterialX Node |
|-------------------|----------------|
| ADD | `ND_add_vector3` |
| SUBTRACT | `ND_subtract_vector3` |
| MULTIPLY | `ND_multiply_vector3` |
| DIVIDE | `ND_divide_vector3` |
| DOT_PRODUCT | `ND_dotproduct_vector3` |
| CROSS_PRODUCT | `ND_crossproduct_vector3` |
| NORMALIZE | `ND_normalize_vector3` |
| LENGTH | `ND_magnitude_vector3` |

---

### Math (`ShaderNodeMath`)

**Blender Node:**
- Inputs: `Value` (multiple)
- Output: `Value`

**MaterialX Translation:**

With constant inputs, results are **pre-computed**. With dynamic inputs:

| Blender Operation | MaterialX Node |
|-------------------|----------------|
| ADD | `ND_add_float` |
| SUBTRACT | `ND_subtract_float` |
| MULTIPLY | `ND_multiply_float` |
| DIVIDE | `ND_divide_float` |
| POWER | `ND_power_float` |
| SQRT | `ND_sqrt_float` |
| ABSOLUTE | `ND_absval_float` |
| MINIMUM | `ND_min_float` |
| MAXIMUM | `ND_max_float` |
| FLOOR | `ND_floor_float` |
| CEIL | `ND_ceil_float` |
| ROUND | `ND_round_float` |
| SINE | `ND_sin_float` |
| COSINE | `ND_cos_float` |
| TANGENT | `ND_tan_float` |

---

### Clamp (`ShaderNodeClamp`)

**Blender Node:**
- Inputs: `Value`, `Min`, `Max`
- Output: `Result`

**MaterialX Translation:**
```
┌─────────────────┐
│ ND_clamp_float  │
│ in=Value        │────▶ Output
│ low=Min         │
│ high=Max        │
└─────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_clamp_float` | Clamp value between min and max |

---

### Map Range (`ShaderNodeMapRange`)

**Blender Node:**
- Inputs: `Value`, `From Min`, `From Max`, `To Min`, `To Max`
- Output: `Result`

**MaterialX Translation:**
```
┌────────────────────┐
│ ND_remap_float     │
│ in=Value           │────▶ Output
│ inlow=From Min     │
│ inhigh=From Max    │
│ outlow=To Min      │
│ outhigh=To Max     │
└────────────────────┘
```

| MaterialX Node | Purpose |
|----------------|---------|
| `ND_remap_float` | Remap value from one range to another |

---

## Geometry Nodes (Auto-generated)

Blender automatically generates geometry input nodes for proper material shading:

```
┌────────────────────┐     ┌─────────────────────┐
│ ND_normal_vector3  │────▶│ ND_normalize_vector3│────▶ Normal
│ space="world"      │     └─────────────────────┘
└────────────────────┘

┌────────────────────┐     ┌─────────────────────┐     ┌────────────────────┐
│ ND_tangent_vector3 │────▶│ ND_normalize_vector3│────▶│ ND_rotate3d_vector3│────▶ Tangent
│ space="world"      │     └─────────────────────┘     │ amount=-90         │
└────────────────────┘                                 │ axis=Normal        │
                                                       └────────────────────┘
```

---

## MaterialX Standard Library Nodes Used

| Node ID | Type | Description |
|---------|------|-------------|
| `ND_constant_color3` | Color | Constant color value |
| `ND_constant_float` | Float | Constant float value |
| `ND_add_color3` | Color | Add two colors |
| `ND_subtract_color3` | Color | Subtract colors |
| `ND_multiply_color3` | Color | Multiply colors |
| `ND_power_color3` | Color | Power (gamma) |
| `ND_max_color3` | Color | Maximum (clamp) |
| `ND_mix_color3` | Color | Linear interpolation |
| `ND_luminance_color3` | Color→Float | RGB to luminance |
| `ND_extract_color3` | Color→Float | Extract channel |
| `ND_combine3_color3` | Float→Color | Combine to color |
| `ND_hsvadjust_color3` | Color | HSV adjustment |
| `ND_add_vector3` | Vector | Add vectors |
| `ND_subtract_vector3` | Vector | Subtract vectors |
| `ND_multiply_vector3` | Vector | Multiply vectors |
| `ND_normalize_vector3` | Vector | Normalize vector |
| `ND_extract_vector3` | Vector→Float | Extract component |
| `ND_combine3_vector3` | Float→Vector | Combine to vector |
| `ND_rotate3d_vector3` | Vector | Rotate vector |
| `ND_convert_color3_vector3` | Color→Vector | Type conversion |
| `ND_convert_vector3_color3` | Vector→Color | Type conversion |
| `ND_normal_vector3` | Geometry | Surface normal |
| `ND_tangent_vector3` | Geometry | Surface tangent |
| `ND_add_float` | Float | Add floats |
| `ND_subtract_float` | Float | Subtract floats |
| `ND_multiply_float` | Float | Multiply floats |
| `ND_divide_float` | Float | Divide floats |
| `ND_clamp_float` | Float | Clamp value |
| `ND_remap_float` | Float | Remap range |

---

## Surface Shader

Blender exports the **Principled BSDF** node as:

| USD Target | MaterialX Node ID |
|------------|-------------------|
| UsdPreviewSurface | `UsdPreviewSurface` |
| MaterialX Surface | `ND_open_pbr_surface_surfaceshader` |

The OpenPBR surface shader includes inputs for:
- `base_color`, `base_metalness`, `base_weight`
- `specular_color`, `specular_roughness`, `specular_ior`
- `coat_color`, `coat_weight`, `coat_roughness`
- `transmission_weight`, `transmission_color`
- `emission_color`, `emission_luminance`
- `geometry_normal`, `geometry_tangent`, `geometry_opacity`

---

## Limitations

### Unsupported or Partially Supported Nodes

| Blender Node | Status | Notes |
|--------------|--------|-------|
| **RGB Curves** | Partial | Only works with constant inputs (pre-computed). Dynamic curve evaluation not supported. |
| **ColorRamp** | Partial | Only works with constant inputs. Dynamic gradient lookup not exported as MaterialX nodes. |
| **Vector Curves** | Partial | Pre-computed only, similar to RGB Curves. |
| **Float Curve** | Partial | Pre-computed only. |
| **Script (OSL)** | Not Supported | OSL scripts cannot be converted to MaterialX. |
| **Custom Group** | Not Supported | Custom node groups are not expanded to MaterialX. |
| **Shader to RGB** | Not Supported | EEVEE-specific, no MaterialX equivalent. |
| **Bevel** | Not Supported | Geometry-based effect, not a shader operation. |
| **Ambient Occlusion** | Limited | Cycles-specific raytracing, limited MaterialX support. |
| **Light Path** | Not Supported | Render engine specific, no direct MaterialX equivalent. |
| **Particle Info** | Not Supported | Runtime particle data not available in MaterialX. |
| **Point Info** | Not Supported | Point cloud specific. |
| **Hair Info** | Not Supported | Hair/fur specific rendering data. |

### MixRGB Blend Modes

The `ShaderNodeMixRGB` node supports multiple blend modes, but **only `MIX` mode** is fully translated to MaterialX:

| Blend Mode | MaterialX Support |
|------------|-------------------|
| Mix | `ND_mix_color3` (full support) |
| Darken | Not directly supported |
| Multiply | Can use `ND_multiply_color3` |
| Burn | Not directly supported |
| Lighten | Not directly supported |
| Screen | Not directly supported |
| Dodge | Not directly supported |
| Add | Can use `ND_add_color3` |
| Overlay | Not directly supported |
| Soft Light | Not directly supported |
| Linear Light | Not directly supported |
| Difference | Can use `ND_subtract_color3` + `ND_absval_color3` |
| Subtract | Can use `ND_subtract_color3` |
| Divide | Can use `ND_divide_color3` |
| Hue | Not directly supported |
| Saturation | Not directly supported |
| Color | Not directly supported |
| Value | Not directly supported |

### Texture Nodes

| Texture Node | MaterialX Support |
|--------------|-------------------|
| Image Texture | `ND_image_color3` / `ND_image_float` |
| Environment Texture | `ND_image_color3` with spherical mapping |
| Noise Texture | `ND_noise3d_float` (different algorithm) |
| Voronoi Texture | `ND_cellnoise3d_float` (approximation) |
| Musgrave Texture | Not directly supported |
| Wave Texture | Not directly supported |
| Magic Texture | Not directly supported |
| Checker Texture | Not directly supported |
| Brick Texture | Not directly supported |
| Gradient Texture | Partial via `ND_ramp*` nodes |
| IES Texture | Not supported |

### Color Space Handling

- Blender assumes **sRGB** for color inputs and **linear** for data
- MaterialX has explicit colorspace attributes but Blender export may not set them correctly
- Manual colorspace conversion may be needed: `ND_srgb_to_linear_color3` / `ND_linear_to_srgb_color3`

### Coordinate System Differences

| Aspect | Blender | MaterialX/USD |
|--------|---------|---------------|
| Up Axis | Z-up | Y-up (configurable) |
| Normal Space | World space exported | May need object/tangent space |
| UV Origin | Bottom-left | Top-left in some renderers |

---

## Known Issues

1. **Constant Folding**: Blender aggressively pre-computes constant expressions. This means simple test setups with only `Value` or `RGB` input nodes may not generate MaterialX node graphs - the result is baked directly into the surface shader input.

2. **Alpha Channel**: Blender uses `color4` (RGBA) internally, but MaterialX often uses `color3`. Alpha handling may be inconsistent.

3. **Node Naming**: MaterialX node names are auto-generated (e.g., `bnode__Invert_Color`, `node_001`) and may not match Blender node names.

4. **Duplicate Materials**: The default Blender cube material is always exported alongside custom materials.

5. **Geometry Nodes Overhead**: Every material exports normal/tangent computation nodes even if not used by the shader.

6. **Fac Input Handling**: When `Fac=1.0` (default), Blender may optimize away the mix operation entirely.

7. **UsdPreviewSurface Fallback**: Both `UsdPreviewSurface` and `ND_open_pbr_surface_surfaceshader` are exported. Some renderers may only support one.

---

## Extra Notes

### MaterialX Version Compatibility

- Blender 5.0 exports MaterialX version **1.39**
- Older MaterialX implementations may not support all nodes
- `ND_open_pbr_surface_surfaceshader` requires MaterialX 1.39+

### OpenPBR vs Standard Surface

Blender uses **OpenPBR Surface** (`ND_open_pbr_surface_surfaceshader`), not the older `ND_standard_surface_surfaceshader`. Key differences:

| Feature | OpenPBR | Standard Surface |
|---------|---------|------------------|
| Base model | OpenPBR spec | Autodesk Standard Surface |
| Metalness | `base_metalness` | `metalness` |
| Emission | `emission_luminance` (nits) | `emission` (multiplier) |
| Coat | Full coat layer | Basic clearcoat |
| Fuzz | `fuzz_weight`, `fuzz_color` | Not available |

### Testing MaterialX Output

To verify MaterialX translation:

```bash
# View MaterialX nodes in USD file
grep -A 5 'def Shader' exported.usda | grep 'info:id'

# Check for NodeGraph sections
grep -B 2 -A 20 'def NodeGraph' exported.usda
```

### Recommended Workflow

1. **Use linked inputs** (not just constants) to force MaterialX node generation
2. **Check export** with `generate_materialx_network=True`
3. **Verify** that both UsdPreviewSurface and MaterialX outputs are present
4. **Test** in target renderer (MaterialX viewer, Hydra, etc.)

### Blender Export Options

```python
bpy.ops.wm.usd_export(
    filepath="output.usda",
    export_materials=True,
    generate_materialx_network=True,  # Enable MaterialX
    export_textures=True,
    overwrite_textures=False,
    relative_paths=True
)
```

### File References

Test USD files with MaterialX translations are available in:
- `tests/feat/node-mtlx/*.usda`

---

## References

- [MaterialX Specification](https://materialx.org/Specification.html)
- [MaterialX Standard Library](https://github.com/AcademySoftwareFoundation/MaterialX/tree/main/libraries/stdlib)
- [OpenPBR Surface Specification](https://academysoftwarefoundation.github.io/OpenPBR/)
- [Blender USD Export Documentation](https://docs.blender.org/manual/en/latest/files/import_export/usd.html)
- [USD MaterialX Schema](https://openusd.org/release/api/usd_mtlx_page_front.html)

---

*Generated from Blender 5.0 USD export analysis*
*Test files exported: 2024*
