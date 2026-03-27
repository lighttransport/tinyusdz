# Blender Shader Nodes Reference

This document lists all Blender shader nodes with their inputs, outputs, and socket types.

**Total nodes: 98**

## Table of Contents

- [BSDF/Shader](#bsdfshader)
- [Texture](#texture)
- [Color](#color)
- [Vector](#vector)
- [Converter](#converter)
- [Input](#input)
- [Output](#output)
- [Volume](#volume)
- [Other](#other)

## Socket Types

| Type | Socket Class | Description | Unit |
|------|-------------|-------------|------|
| `SHADER` | NodeSocketShader | Shader/closure data | - |
| `RGBA` | NodeSocketColor | RGBA color (4 floats, 0.0-1.0) | - |
| `VECTOR` | NodeSocketVector | 3D vector (XYZ) | scene units |
| `VALUE` | NodeSocketFloat | Single float value | varies |
| `INT` | NodeSocketInt | Integer value | - |
| `BOOLEAN` | NodeSocketBool | Boolean value | - |
| `STRING` | NodeSocketString | String value | - |
| `ROTATION` | NodeSocketRotation | Euler rotation (XYZ) | **radians** |

### ROTATION Type Details

The `ROTATION` socket type stores rotation as **Euler angles in radians** internally:

| Property | Value |
|----------|-------|
| Internal storage | Radians (Euler angles) |
| Data structure | `Euler(x, y, z)` with order 'XYZ' |
| Default value format | `<Euler (x=0.0000, y=0.0000, z=0.0000), order='XYZ'>` |
| UI display | May show degrees (based on scene unit settings) |

**Usage notes:**
- When setting values programmatically, use **radians**
- Blender's UI may display degrees, but underlying values are always radians
- Convert with `math.radians(deg)` or `math.degrees(rad)`
- Only used in `ShaderNodeMix` (when mixing rotations)

```python
import math
# Set 90 degrees rotation on X axis
socket.default_value.x = math.radians(90)  # = 1.5708 radians
```

## Connectability

- `link_limit`: Maximum number of connections (1 for inputs, 4095 for outputs typically)
- Inputs can receive one connection
- Outputs can connect to multiple inputs

## BSDF/Shader

### Add Shader (`ShaderNodeAddShader`)

**Description:** Add two Shaders together

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Shader | `Shader` | `SHADER` | `NodeSocketShader` | - | 1 |
| Shader | `Shader_001` | `SHADER` | `NodeSocketShader` | - | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Shader | `Shader` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Background (`ShaderNodeBackground`)

**Description:** Add background light emission.
Note: This node should only be used for the world surface output

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Strength | `Strength` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Background | `Background` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Glossy BSDF (`ShaderNodeBsdfAnisotropic`)

**Description:** Reflection with microfacet distribution, used for materials such as metal or mirrors

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Rotation | `Rotation` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Tangent | `Tangent` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Diffuse BSDF (`ShaderNodeBsdfDiffuse`)

**Description:** Lambertian and Oren-Nayar diffuse reflection

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Glass BSDF (`ShaderNodeBsdfGlass`)

**Description:** Glass-like shader mixing refraction and reflection at grazing angles

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloat` | 1.500 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Thin Film Thickness | `Thin Film Thickness` | `VALUE` | `NodeSocketFloatWavelength` | 0.000 | 1 |
| Thin Film IOR | `Thin Film IOR` | `VALUE` | `NodeSocketFloat` | 1.330 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Hair BSDF (`ShaderNodeBsdfHair`)

**Description:** Reflection and transmission shaders optimized for hair rendering

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Offset | `Offset` | `VALUE` | `NodeSocketFloatAngle` | 0.000 | 1 |
| RoughnessU | `RoughnessU` | `VALUE` | `NodeSocketFloatFactor` | 0.100 | 1 |
| RoughnessV | `RoughnessV` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Tangent | `Tangent` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Principled Hair BSDF (`ShaderNodeBsdfHairPrincipled`)

**Description:** Physically-based, easy-to-use shader for rendering hair and fur

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.02, 0.01, 0.00, 1.00 | 1 |
| Melanin | `Melanin` | `VALUE` | `NodeSocketFloatFactor` | 0.800 | 1 |
| Melanin Redness | `Melanin Redness` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Tint | `Tint` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Absorption Coefficient | `Absorption Coefficient` | `VECTOR` | `NodeSocketVector` | 0.25, 0.52, 1.37 | 1 |
| Aspect Ratio | `Aspect Ratio` | `VALUE` | `NodeSocketFloatFactor` | 0.850 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.300 | 1 |
| Radial Roughness | `Radial Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.300 | 1 |
| Coat | `Coat` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloat` | 1.550 | 1 |
| Offset | `Offset` | `VALUE` | `NodeSocketFloatAngle` | 0.035 | 1 |
| Random Color | `Random Color` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Random Roughness | `Random Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Random | `Random` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Reflection | `R lobe` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Transmission | `TT lobe` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Secondary Reflection | `TRT lobe` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Metallic BSDF (`ShaderNodeBsdfMetallic`)

**Description:** Metallic reflection with microfacet distribution, and metallic fresnel

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Base Color | `Base Color` | `RGBA` | `NodeSocketColor` | 0.62, 0.58, 0.54, 1.00 | 1 |
| Edge Tint | `Edge Tint` | `RGBA` | `NodeSocketColor` | 0.69, 0.73, 0.77, 1.00 | 1 |
| IOR | `IOR` | `VECTOR` | `NodeSocketVector` | 2.76, 2.51, 2.23 | 1 |
| Extinction | `Extinction` | `VECTOR` | `NodeSocketVector` | 3.87, 3.40, 3.01 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Rotation | `Rotation` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Tangent | `Tangent` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Thin Film Thickness | `Thin Film Thickness` | `VALUE` | `NodeSocketFloatWavelength` | 0.000 | 1 |
| Thin Film IOR | `Thin Film IOR` | `VALUE` | `NodeSocketFloat` | 1.330 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Principled BSDF (`ShaderNodeBsdfPrincipled`)

**Description:** Physically-based, easy-to-use shader for rendering surface materials, based on the OpenPBR model

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Base Color | `Base Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Metallic | `Metallic` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloat` | 1.500 | 1 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Diffuse Roughness | `Diffuse Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Subsurface Weight | `Subsurface Weight` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Subsurface Radius | `Subsurface Radius` | `VECTOR` | `NodeSocketVector` | 1.00, 0.20, 0.10 | 1 |
| Subsurface Scale | `Subsurface Scale` | `VALUE` | `NodeSocketFloatDistance` | 0.050 | 1 |
| Subsurface IOR | `Subsurface IOR` | `VALUE` | `NodeSocketFloatFactor` | 1.400 | 1 |
| Subsurface Anisotropy | `Subsurface Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Specular IOR Level | `Specular IOR Level` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Specular Tint | `Specular Tint` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Anisotropic | `Anisotropic` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Anisotropic Rotation | `Anisotropic Rotation` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Tangent | `Tangent` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Transmission Weight | `Transmission Weight` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Coat Weight | `Coat Weight` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Coat Roughness | `Coat Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.030 | 1 |
| Coat IOR | `Coat IOR` | `VALUE` | `NodeSocketFloat` | 1.500 | 1 |
| Coat Tint | `Coat Tint` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Coat Normal | `Coat Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Sheen Weight | `Sheen Weight` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Sheen Roughness | `Sheen Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Sheen Tint | `Sheen Tint` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Emission Color | `Emission Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Emission Strength | `Emission Strength` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Thin Film Thickness | `Thin Film Thickness` | `VALUE` | `NodeSocketFloatWavelength` | 0.000 | 1 |
| Thin Film IOR | `Thin Film IOR` | `VALUE` | `NodeSocketFloat` | 1.330 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Ray Portal BSDF (`ShaderNodeBsdfRayPortal`)

**Description:** Continue tracing from an arbitrary new position and in a new direction

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Position | `Position` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Direction | `Direction` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Refraction BSDF (`ShaderNodeBsdfRefraction`)

**Description:** Glossy refraction with sharp or microfacet distribution, typically used for materials that transmit light

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloat` | 1.450 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Sheen BSDF (`ShaderNodeBsdfSheen`)

**Description:** Reflection for materials such as cloth.
Typically mixed with other shaders (such as a Diffuse Shader) and is not particularly useful on its own

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Toon BSDF (`ShaderNodeBsdfToon`)

**Description:** Diffuse and Glossy shaders with cartoon light effects

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Size | `Size` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Smooth | `Smooth` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Translucent BSDF (`ShaderNodeBsdfTranslucent`)

**Description:** Lambertian diffuse transmission

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Transparent BSDF (`ShaderNodeBsdfTransparent`)

**Description:** Transparency without refraction, passing straight through the surface as if there were no geometry

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Specular BSDF (`ShaderNodeEeveeSpecular`)

**Description:** Similar to the Principled BSDF node but uses the specular workflow instead of metallic, which functions by specifying the facing (along normal) reflection color. Energy is not conserved, so the result may not be physically accurate

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Base Color | `Base Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Specular | `Specular` | `RGBA` | `NodeSocketColor` | 0.03, 0.03, 0.03, 1.00 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.200 | 1 |
| Emissive Color | `Emissive Color` | `RGBA` | `NodeSocketColor` | 0.00, 0.00, 0.00, 1.00 | 1 |
| Transparency | `Transparency` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Clear Coat | `Clear Coat` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Clear Coat Roughness | `Clear Coat Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Clear Coat Normal | `Clear Coat Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSDF | `BSDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Emission (`ShaderNodeEmission`)

**Description:** Lambertian emission shader

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Strength | `Strength` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Emission | `Emission` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Holdout (`ShaderNodeHoldout`)

**Description:** Create a "hole" in the image with zero alpha transparency, which is useful for compositing.
Note: the holdout shader can only create alpha when transparency is enabled in the film settings

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Holdout | `Holdout` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Mix Shader (`ShaderNodeMixShader`)

**Description:** Mix two shaders together. Typically used for material layering

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Shader | `Shader` | `SHADER` | `NodeSocketShader` | - | 1 |
| Shader | `Shader_001` | `SHADER` | `NodeSocketShader` | - | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Shader | `Shader` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Subsurface Scattering (`ShaderNodeSubsurfaceScattering`)

**Description:** Subsurface multiple scattering shader to simulate light entering the surface and bouncing internally.
Typically used for materials such as skin, wax, marble or milk

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 0.050 | 1 |
| Radius | `Radius` | `VECTOR` | `NodeSocketVector` | 1.00, 0.20, 0.10 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloatFactor` | 1.400 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| BSSRDF | `BSSRDF` | `SHADER` | `NodeSocketShader` | 4095 |

---

## Texture

### Brick Texture (`ShaderNodeTexBrick`)

**Description:** Generate a procedural texture producing bricks

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Color1 | `Color1` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Color2 | `Color2` | `RGBA` | `NodeSocketColor` | 0.20, 0.20, 0.20, 1.00 | 1 |
| Mortar | `Mortar` | `RGBA` | `NodeSocketColor` | 0.00, 0.00, 0.00, 1.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Mortar Size | `Mortar Size` | `VALUE` | `NodeSocketFloat` | 0.020 | 1 |
| Mortar Smooth | `Mortar Smooth` | `VALUE` | `NodeSocketFloat` | 0.100 | 1 |
| Bias | `Bias` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Brick Width | `Brick Width` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Row Height | `Row Height` | `VALUE` | `NodeSocketFloat` | 0.250 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Checker Texture (`ShaderNodeTexChecker`)

**Description:** Generate a checkerboard texture

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Color1 | `Color1` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Color2 | `Color2` | `RGBA` | `NodeSocketColor` | 0.20, 0.20, 0.20, 1.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Texture Coordinate (`ShaderNodeTexCoord`)

**Description:** Retrieve multiple types of texture coordinates.
Typically used as inputs for texture nodes

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Generated | `Generated` | `VECTOR` | `NodeSocketVector` | 4095 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 4095 |
| UV | `UV` | `VECTOR` | `NodeSocketVector` | 4095 |
| Object | `Object` | `VECTOR` | `NodeSocketVector` | 4095 |
| Camera | `Camera` | `VECTOR` | `NodeSocketVector` | 4095 |
| Window | `Window` | `VECTOR` | `NodeSocketVector` | 4095 |
| Reflection | `Reflection` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Environment Texture (`ShaderNodeTexEnvironment`)

**Description:** Sample an image file as an environment texture. Typically used to light the scene with the background node

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Gabor Texture (`ShaderNodeTexGabor`)

**Description:** Generate Gabor noise

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Frequency | `Frequency` | `VALUE` | `NodeSocketFloat` | 2.000 | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Orientation | `Orientation 2D` | `VALUE` | `NodeSocketFloatAngle` | 0.785 | 1 |
| Orientation | `Orientation 3D` | `VECTOR` | `NodeSocketVectorDirection` | <Vector (0.0000, -0.0000, 0.0000)> | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |
| Phase | `Phase` | `VALUE` | `NodeSocketFloat` | 4095 |
| Intensity | `Intensity` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Gradient Texture (`ShaderNodeTexGradient`)

**Description:** Generate interpolated color and intensity values based on the input vector

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### IES Texture (`ShaderNodeTexIES`)

**Description:** Match real world lights with IES files, which store the directional intensity distribution of light sources

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Strength | `Strength` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Image Texture (`ShaderNodeTexImage`)

**Description:** Sample an image file as a texture

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Magic Texture (`ShaderNodeTexMagic`)

**Description:** Generate a psychedelic color texture

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Distortion | `Distortion` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Noise Texture (`ShaderNodeTexNoise`)

**Description:** Generate fractal Perlin noise

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| W | `W` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Detail | `Detail` | `VALUE` | `NodeSocketFloat` | 2.000 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Lacunarity | `Lacunarity` | `VALUE` | `NodeSocketFloat` | 2.000 | 1 |
| Offset | `Offset` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Gain | `Gain` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Distortion | `Distortion` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Sky Texture (`ShaderNodeTexSky`)

**Description:** Generate a procedural sky texture

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Voronoi Texture (`ShaderNodeTexVoronoi`)

**Description:** Generate Worley noise based on the distance to random points. Typically used to generate textures such as stones, water, or biological cells

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| W | `W` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Detail | `Detail` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Roughness | `Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Lacunarity | `Lacunarity` | `VALUE` | `NodeSocketFloat` | 2.000 | 1 |
| Smoothness | `Smoothness` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Exponent | `Exponent` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Randomness | `Randomness` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Distance | `Distance` | `VALUE` | `NodeSocketFloat` | 4095 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Position | `Position` | `VECTOR` | `NodeSocketVector` | 4095 |
| W | `W` | `VALUE` | `NodeSocketFloat` | 4095 |
| Radius | `Radius` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Wave Texture (`ShaderNodeTexWave`)

**Description:** Generate procedural bands or rings with noise

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Distortion | `Distortion` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Detail | `Detail` | `VALUE` | `NodeSocketFloat` | 2.000 | 1 |
| Detail Scale | `Detail Scale` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Detail Roughness | `Detail Roughness` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Phase Offset | `Phase Offset` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### White Noise Texture (`ShaderNodeTexWhiteNoise`)

**Description:** Calculate a random value or color based on an input seed

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| W | `W` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

## Color

### Brightness/Contrast (`ShaderNodeBrightContrast`)

**Description:** Control the brightness and contrast of the input color

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Brightness | `Bright` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Contrast | `Contrast` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Combine Color (`ShaderNodeCombineColor`)

**Description:** Create a color from individual components using multiple models

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Red | `Red` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Green | `Green` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Blue | `Blue` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Gamma (`ShaderNodeGamma`)

**Description:** Apply a gamma correction

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Gamma | `Gamma` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Hue/Saturation/Value (`ShaderNodeHueSaturation`)

**Description:** Apply a color transformation in the HSV color model

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Hue | `Hue` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Saturation | `Saturation` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Invert Color (`ShaderNodeInvert`)

**Description:** Invert a color, producing a negative

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.00, 0.00, 0.00, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Mix (`ShaderNodeMix`)

**Description:** Mix values by a factor

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Factor_Float` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Factor | `Factor_Vector` | `VECTOR` | `NodeSocketVectorFactor` | 0.50, 0.50, 0.50 | 1 |
| A | `A_Float` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| B | `B_Float` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| A | `A_Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| B | `B_Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| A | `A_Color` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 0.50, 1.00 | 1 |
| B | `B_Color` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 0.50, 1.00 | 1 |
| A | `A_Rotation` | `ROTATION` | `NodeSocketRotation` | <Euler (x=0.0000, y=1.0000, z=0.0000), order='XYZ'> | 1 |
| B | `B_Rotation` | `ROTATION` | `NodeSocketRotation` | <Euler (x=0.0000, y=0.0000, z=216918766125056.0000), order='XYZ'> | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Result | `Result_Float` | `VALUE` | `NodeSocketFloat` | 4095 |
| Result | `Result_Vector` | `VECTOR` | `NodeSocketVector` | 4095 |
| Result | `Result_Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Result | `Result_Rotation` | `ROTATION` | `NodeSocketRotation` | 4095 |

---

### Mix (Legacy) (`ShaderNodeMixRGB`)

**Description:** Mix two input colors

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |
| Color1 | `Color1` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 0.50, 1.00 | 1 |
| Color2 | `Color2` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 0.50, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Color (`ShaderNodeRGB`)

**Description:** A color picker

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### RGB Curves (`ShaderNodeRGBCurve`)

**Description:** Apply color corrections for each color channel

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### RGB to BW (`ShaderNodeRGBToBW`)

**Description:** Convert a color's luminance to a grayscale value

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 0.50, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Val | `Val` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Separate Color (`ShaderNodeSeparateColor`)

**Description:** Split a color into its individual components using multiple models

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Red | `Red` | `VALUE` | `NodeSocketFloat` | 4095 |
| Green | `Green` | `VALUE` | `NodeSocketFloat` | 4095 |
| Blue | `Blue` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Shader to RGB (`ShaderNodeShaderToRGB`)

**Description:** Convert rendering effect (such as light and shadow) to color. Typically used for non-photorealistic rendering, to apply additional effects on the output of BSDFs.
Note: only supported in EEVEE

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Shader | `Shader` | `SHADER` | `NodeSocketShader` | - | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Color Ramp (`ShaderNodeValToRGB`)

**Description:** Map values to colors with the use of a gradient

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 0.500 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Color Attribute (`ShaderNodeVertexColor`)

**Description:** Retrieve a color attribute, or the default fallback if none is specified

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 4095 |

---

## Vector

### Bump (`ShaderNodeBump`)

**Description:** Generate a perturbed normal from a height texture for bump mapping. Typically used for faking highly detailed surfaces

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Strength | `Strength` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Distance | `Distance` | `VALUE` | `NodeSocketFloat` | 0.001 | 1 |
| Filter Width | `Filter Width` | `VALUE` | `NodeSocketFloat` | 0.100 | 1 |
| Height | `Height` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Combine XYZ (`ShaderNodeCombineXYZ`)

**Description:** Create a vector from X, Y, and Z components

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| X | `X` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Y | `Y` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Z | `Z` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Displacement (`ShaderNodeDisplacement`)

**Description:** Displace the surface along the surface normal

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Height | `Height` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Midlevel | `Midlevel` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 0.010 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Displacement | `Displacement` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Mapping (`ShaderNodeMapping`)

**Description:** Transform the input vector by applying translation, rotation, and scale

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Location | `Location` | `VECTOR` | `NodeSocketVectorTranslation` | <Vector (0.0000, 0.0000, 0.0000)> | 1 |
| Rotation | `Rotation` | `VECTOR` | `NodeSocketVectorEuler` | <Euler (x=0.0000, y=-0.0000, z=0.0000), order='XYZ'> | 1 |
| Scale | `Scale` | `VECTOR` | `NodeSocketVectorXYZ` | <Vector (0.0000, -1.0000, 1.0000)> | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Normal (`ShaderNodeNormal`)

**Description:** Generate a normal vector and a dot product

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Normal | `Normal` | `VECTOR` | `NodeSocketVectorDirection` | <Vector (0.0000, -0.0000, 0.0000)> | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Normal | `Normal` | `VECTOR` | `NodeSocketVectorDirection` | 4095 |
| Dot | `Dot` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Normal Map (`ShaderNodeNormalMap`)

**Description:** Generate a perturbed normal from an RGB normal map image. Typically used for faking highly detailed surfaces

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Strength | `Strength` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 1.00, 1.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Separate XYZ (`ShaderNodeSeparateXYZ`)

**Description:** Split a vector into its X, Y, and Z components

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| X | `X` | `VALUE` | `NodeSocketFloat` | 4095 |
| Y | `Y` | `VALUE` | `NodeSocketFloat` | 4095 |
| Z | `Z` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Tangent (`ShaderNodeTangent`)

**Description:** Generate a tangent direction for the Anisotropic BSDF

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Tangent | `Tangent` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Vector Curves (`ShaderNodeVectorCurve`)

**Description:** Map input vector components with curves

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Vector Displacement (`ShaderNodeVectorDisplacement`)

**Description:** Displace the surface along an arbitrary direction

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Midlevel | `Midlevel` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 0.010 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Displacement | `Displacement` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Vector Math (`ShaderNodeVectorMath`)

**Description:** Perform vector math operation

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Vector | `Vector_001` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Vector | `Vector_002` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Scale | `Scale` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Vector Rotate (`ShaderNodeVectorRotate`)

**Description:** Rotate a vector around a pivot point (center)

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Center | `Center` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Axis | `Axis` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 1.00 | 1 |
| Angle | `Angle` | `VALUE` | `NodeSocketFloatAngle` | 0.000 | 1 |
| Rotation | `Rotation` | `VECTOR` | `NodeSocketVectorEuler` | <Euler (x=4563468295454047391545556992000.0000, y=0.0000, z=0.0000), order='XYZ'> | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Vector Transform (`ShaderNodeVectorTransform`)

**Description:** Convert a vector, point, or normal between world, camera, and object coordinate space

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.50, 0.50, 0.50 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |

---

## Converter

### Blackbody (`ShaderNodeBlackbody`)

**Description:** Convert a blackbody temperature to an RGB value

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Temperature | `Temperature` | `VALUE` | `NodeSocketFloatColorTemperature` | 6500.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

### Clamp (`ShaderNodeClamp`)

**Description:** Clamp a value between a minimum and a maximum

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Min | `Min` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Max | `Max` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Result | `Result` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Float Curve (`ShaderNodeFloatCurve`)

**Description:** Map an input float to a curve and outputs a float value

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Factor | `Factor` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Map Range (`ShaderNodeMapRange`)

**Description:** Remap a value from a range to a target range

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| From Min | `From Min` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| From Max | `From Max` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| To Min | `To Min` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| To Max | `To Max` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Steps | `Steps` | `VALUE` | `NodeSocketFloat` | 4.000 | 1 |
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| From Min | `From_Min_FLOAT3` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| From Max | `From_Max_FLOAT3` | `VECTOR` | `NodeSocketVector` | 1.00, 1.00, 1.00 | 1 |
| To Min | `To_Min_FLOAT3` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| To Max | `To_Max_FLOAT3` | `VECTOR` | `NodeSocketVector` | 1.00, 1.00, 1.00 | 1 |
| Steps | `Steps_FLOAT3` | `VECTOR` | `NodeSocketVector` | 4.00, 4.00, 4.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Result | `Result` | `VALUE` | `NodeSocketFloat` | 4095 |
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Math (`ShaderNodeMath`)

**Description:** Perform math operations

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Value | `Value_001` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Value | `Value_002` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Value (`ShaderNodeValue`)

**Description:** Input numerical values to other nodes in the tree

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Wavelength (`ShaderNodeWavelength`)

**Description:** Convert a wavelength value to an RGB value

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Wavelength | `Wavelength` | `VALUE` | `NodeSocketFloatWavelength` | 500.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |

---

## Input

### Ambient Occlusion (`ShaderNodeAmbientOcclusion`)

**Description:** Compute how much the hemisphere above the shading point is occluded, for example to add weathering effects to corners.
Note: For Cycles, this may slow down renders significantly

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Distance | `Distance` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| AO | `AO` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Attribute (`ShaderNodeAttribute`)

**Description:** Retrieve attributes attached to objects or geometry

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Vector | `Vector` | `VECTOR` | `NodeSocketVector` | 4095 |
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Bevel (`ShaderNodeBevel`)

**Description:** Generates normals with round corners.
Note: only supported in Cycles, and may slow down renders

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Radius | `Radius` | `VALUE` | `NodeSocketFloat` | 0.050 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Camera Data (`ShaderNodeCameraData`)

**Description:** Retrieve information about the camera and how it relates to the current shading point's position

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| View Vector | `View Vector` | `VECTOR` | `NodeSocketVector` | 4095 |
| View Z Depth | `View Z Depth` | `VALUE` | `NodeSocketFloat` | 4095 |
| View Distance | `View Distance` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Fresnel (`ShaderNodeFresnel`)

**Description:** Produce a blending factor depending on the angle between the surface normal and the view direction using Fresnel equations.
Typically used for mixing reflections at grazing angles

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| IOR | `IOR` | `VALUE` | `NodeSocketFloat` | 1.500 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Curves Info (`ShaderNodeHairInfo`)

**Description:** Retrieve hair curve information

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Is Strand | `Is Strand` | `VALUE` | `NodeSocketFloat` | 4095 |
| Intercept | `Intercept` | `VALUE` | `NodeSocketFloat` | 4095 |
| Length | `Length` | `VALUE` | `NodeSocketFloat` | 4095 |
| Thickness | `Thickness` | `VALUE` | `NodeSocketFloat` | 4095 |
| Tangent Normal | `Tangent Normal` | `VECTOR` | `NodeSocketVector` | 4095 |
| Random | `Random` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Layer Weight (`ShaderNodeLayerWeight`)

**Description:** Produce a blending factor depending on the angle between the surface normal and the view direction.
Typically used for layering shaders with the Mix Shader node

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Blend | `Blend` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Fresnel | `Fresnel` | `VALUE` | `NodeSocketFloat` | 4095 |
| Facing | `Facing` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Light Falloff (`ShaderNodeLightFalloff`)

**Description:** Manipulate how light intensity decreases over distance. Typically used for non-physically-based effects; in reality light always falls off quadratically

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Strength | `Strength` | `VALUE` | `NodeSocketFloat` | 100.000 | 1 |
| Smooth | `Smooth` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Quadratic | `Quadratic` | `VALUE` | `NodeSocketFloat` | 4095 |
| Linear | `Linear` | `VALUE` | `NodeSocketFloat` | 4095 |
| Constant | `Constant` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Light Path (`ShaderNodeLightPath`)

**Description:** Retrieve the type of incoming ray for which the shader is being executed.
Typically used for non-physically-based tricks

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Is Camera Ray | `Is Camera Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Shadow Ray | `Is Shadow Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Diffuse Ray | `Is Diffuse Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Glossy Ray | `Is Glossy Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Singular Ray | `Is Singular Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Reflection Ray | `Is Reflection Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Transmission Ray | `Is Transmission Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Is Volume Scatter Ray | `Is Volume Scatter Ray` | `VALUE` | `NodeSocketFloat` | 4095 |
| Ray Length | `Ray Length` | `VALUE` | `NodeSocketFloat` | 4095 |
| Ray Depth | `Ray Depth` | `VALUE` | `NodeSocketFloat` | 4095 |
| Diffuse Depth | `Diffuse Depth` | `VALUE` | `NodeSocketFloat` | 4095 |
| Glossy Depth | `Glossy Depth` | `VALUE` | `NodeSocketFloat` | 4095 |
| Transparent Depth | `Transparent Depth` | `VALUE` | `NodeSocketFloat` | 4095 |
| Transmission Depth | `Transmission Depth` | `VALUE` | `NodeSocketFloat` | 4095 |
| Portal Depth | `Portal Depth` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Geometry (`ShaderNodeNewGeometry`)

**Description:** Retrieve geometric information about the current shading point

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Position | `Position` | `VECTOR` | `NodeSocketVector` | 4095 |
| Normal | `Normal` | `VECTOR` | `NodeSocketVector` | 4095 |
| Tangent | `Tangent` | `VECTOR` | `NodeSocketVector` | 4095 |
| True Normal | `True Normal` | `VECTOR` | `NodeSocketVector` | 4095 |
| Incoming | `Incoming` | `VECTOR` | `NodeSocketVector` | 4095 |
| Parametric | `Parametric` | `VECTOR` | `NodeSocketVector` | 4095 |
| Backfacing | `Backfacing` | `VALUE` | `NodeSocketFloat` | 4095 |
| Pointiness | `Pointiness` | `VALUE` | `NodeSocketFloat` | 4095 |
| Random Per Island | `Random Per Island` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Object Info (`ShaderNodeObjectInfo`)

**Description:** Retrieve information about the object instance

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Location | `Location` | `VECTOR` | `NodeSocketVector` | 4095 |
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 4095 |
| Object Index | `Object Index` | `VALUE` | `NodeSocketFloat` | 4095 |
| Material Index | `Material Index` | `VALUE` | `NodeSocketFloat` | 4095 |
| Random | `Random` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Particle Info (`ShaderNodeParticleInfo`)

**Description:** Retrieve the data of the particle that spawned the object instance, for example to give variation to multiple instances of an object

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Index | `Index` | `VALUE` | `NodeSocketFloat` | 4095 |
| Random | `Random` | `VALUE` | `NodeSocketFloat` | 4095 |
| Age | `Age` | `VALUE` | `NodeSocketFloat` | 4095 |
| Lifetime | `Lifetime` | `VALUE` | `NodeSocketFloat` | 4095 |
| Location | `Location` | `VECTOR` | `NodeSocketVector` | 4095 |
| Size | `Size` | `VALUE` | `NodeSocketFloat` | 4095 |
| Velocity | `Velocity` | `VECTOR` | `NodeSocketVector` | 4095 |
| Angular Velocity | `Angular Velocity` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Point Info (`ShaderNodePointInfo`)

**Description:** Retrieve information about points in a point cloud

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Position | `Position` | `VECTOR` | `NodeSocketVector` | 4095 |
| Radius | `Radius` | `VALUE` | `NodeSocketFloat` | 4095 |
| Random | `Random` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### UV Along Stroke (`ShaderNodeUVAlongStroke`)

**Description:** UV coordinates that map a texture along the stroke length

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| UV | `UV` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### UV Map (`ShaderNodeUVMap`)

**Description:** Retrieve a UV map from the geometry, or the default fallback if none is specified

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| UV | `UV` | `VECTOR` | `NodeSocketVector` | 4095 |

---

### Wireframe (`ShaderNodeWireframe`)

**Description:** Retrieve the edges of an object as it appears to Cycles.
Note: as meshes are triangulated before being processed by Cycles, topology will always appear triangulated

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Size | `Size` | `VALUE` | `NodeSocketFloat` | 0.010 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Factor | `Fac` | `VALUE` | `NodeSocketFloat` | 4095 |

---

## Output

### AOV Output (`ShaderNodeOutputAOV`)

**Description:** Arbitrary Output Variables.
Provide custom render passes for arbitrary shader node outputs

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.00, 0.00, 0.00, 1.00 | 1 |
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

---

### Light Output (`ShaderNodeOutputLight`)

**Description:** Output light information to a light object

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Surface | `Surface` | `SHADER` | `NodeSocketShader` | - | 1 |

---

### Line Style Output (`ShaderNodeOutputLineStyle`)

**Description:** Control the mixing of texture information into the base color of line styles

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 1.00, 0.00, 1.00, 1.00 | 1 |
| Color Fac | `Color Fac` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |
| Alpha Fac | `Alpha Fac` | `VALUE` | `NodeSocketFloatFactor` | 1.000 | 1 |

---

### Material Output (`ShaderNodeOutputMaterial`)

**Description:** Output surface material information for use in rendering

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Surface | `Surface` | `SHADER` | `NodeSocketShader` | - | 1 |
| Volume | `Volume` | `SHADER` | `NodeSocketShader` | - | 1 |
| Displacement | `Displacement` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |
| Thickness | `Thickness` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

---

### World Output (`ShaderNodeOutputWorld`)

**Description:** Output light color information to the scene's World

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Surface | `Surface` | `SHADER` | `NodeSocketShader` | - | 1 |
| Volume | `Volume` | `SHADER` | `NodeSocketShader` | - | 1 |

---

## Volume

### Volume Absorption (`ShaderNodeVolumeAbsorption`)

**Description:** Absorb light as it passes through the volume

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Density | `Density` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Volume | `Volume` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Volume Coefficients (`ShaderNodeVolumeCoefficients`)

**Description:** Model all three physical processes in a volume, represented by their coefficients

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Absorption Coefficients | `Absorption Coefficients` | `VECTOR` | `NodeSocketVector` | 1.00, 1.00, 1.00 | 1 |
| Scatter Coefficients | `Scatter Coefficients` | `VECTOR` | `NodeSocketVector` | 1.00, 1.00, 1.00 | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloatFactor` | 1.330 | 1 |
| Backscatter | `Backscatter` | `VALUE` | `NodeSocketFloatFactor` | 0.100 | 1 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Diameter | `Diameter` | `VALUE` | `NodeSocketFloat` | 20.000 | 1 |
| Emission Coefficients | `Emission Coefficients` | `VECTOR` | `NodeSocketVector` | 0.00, 0.00, 0.00 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Volume | `Volume` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Volume Info (`ShaderNodeVolumeInfo`)

**Description:** Read volume data attributes from volume grids

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 4095 |
| Density | `Density` | `VALUE` | `NodeSocketFloat` | 4095 |
| Flame | `Flame` | `VALUE` | `NodeSocketFloat` | 4095 |
| Temperature | `Temperature` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Principled Volume (`ShaderNodeVolumePrincipled`)

**Description:** Combine all volume shading components into a single easy to use node

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.50, 0.50, 0.50, 1.00 | 1 |
| Color Attribute | `Color Attribute` | `STRING` | `NodeSocketString` |  | 1 |
| Density | `Density` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Density Attribute | `Density Attribute` | `STRING` | `NodeSocketString` | density | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Absorption Color | `Absorption Color` | `RGBA` | `NodeSocketColor` | 0.00, 0.00, 0.00, 1.00 | 1 |
| Emission Strength | `Emission Strength` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Emission Color | `Emission Color` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Blackbody Intensity | `Blackbody Intensity` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| Blackbody Tint | `Blackbody Tint` | `RGBA` | `NodeSocketColor` | 1.00, 1.00, 1.00, 1.00 | 1 |
| Temperature | `Temperature` | `VALUE` | `NodeSocketFloatColorTemperature` | 1000.000 | 1 |
| Temperature Attribute | `Temperature Attribute` | `STRING` | `NodeSocketString` | temperature | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Volume | `Volume` | `SHADER` | `NodeSocketShader` | 4095 |

---

### Volume Scatter (`ShaderNodeVolumeScatter`)

**Description:** Scatter light as it passes through the volume, often used to add fog to a scene

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Color | `Color` | `RGBA` | `NodeSocketColor` | 0.80, 0.80, 0.80, 1.00 | 1 |
| Density | `Density` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Anisotropy | `Anisotropy` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |
| IOR | `IOR` | `VALUE` | `NodeSocketFloatFactor` | 1.330 | 1 |
| Backscatter | `Backscatter` | `VALUE` | `NodeSocketFloatFactor` | 0.100 | 1 |
| Alpha | `Alpha` | `VALUE` | `NodeSocketFloat` | 0.500 | 1 |
| Diameter | `Diameter` | `VALUE` | `NodeSocketFloat` | 20.000 | 1 |
| Weight | `Weight` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Volume | `Volume` | `SHADER` | `NodeSocketShader` | 4095 |

---

## Other

### Group (`ShaderNodeGroup`)

---

### Radial Tiling (`ShaderNodeRadialTiling`)

**Description:** Transform Coordinate System for Radial Tiling

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Vector | `Vector` | `VECTOR` | `NodeSocketVector2D` | 0.00, 0.00 | 1 |
| Sides | `Sides` | `VALUE` | `NodeSocketFloat` | 5.000 | 1 |
| Roundness | `Roundness` | `VALUE` | `NodeSocketFloatFactor` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Segment Coordinates | `Segment Coordinates` | `VECTOR` | `NodeSocketVector` | 4095 |
| Segment ID | `Segment ID` | `VALUE` | `NodeSocketFloat` | 4095 |
| Segment Width | `Segment Width` | `VALUE` | `NodeSocketFloat` | 4095 |
| Segment Rotation | `Segment Rotation` | `VALUE` | `NodeSocketFloat` | 4095 |

---

### Script (`ShaderNodeScript`)

**Description:** Generate an OSL shader from a file or text data-block.
Note: OSL shaders are not supported on all GPU backends

---

### Squeeze Value (Legacy) (`ShaderNodeSqueeze`)

**Description:** Deprecated

**Inputs:**

| Name | Identifier | Type | Socket Class | Default | Link Limit |
|------|------------|------|--------------|---------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |
| Width | `Width` | `VALUE` | `NodeSocketFloat` | 1.000 | 1 |
| Center | `Center` | `VALUE` | `NodeSocketFloat` | 0.000 | 1 |

**Outputs:**

| Name | Identifier | Type | Socket Class | Link Limit |
|------|------------|------|--------------|------------|
| Value | `Value` | `VALUE` | `NodeSocketFloat` | 4095 |

---
