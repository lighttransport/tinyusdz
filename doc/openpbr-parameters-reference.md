# OpenPBR Parameters Reference

## Overview

This document provides a comprehensive mapping of OpenPBR (Open Physically-Based Rendering) parameters as implemented in TinyUSDZ, their corresponding Blender v4.5+ MaterialX export names, and Three.js MeshPhysicalMaterial support status.

**Key Points:**
- OpenPBR is the Academy Software Foundation's open standard for PBR materials
- Blender v4.5+ exports MaterialX with `ND_open_pbr_surface_surfaceshader` node definition
- Three.js MeshPhysicalMaterial has limited support for advanced OpenPBR features
- TinyUSDZ supports full OpenPBR parameter set for parsing and conversion

## Parameter Categories

### Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully supported in Three.js MeshPhysicalMaterial |
| ⚠️ | Partially supported or requires workarounds |
| ❌ | Not supported in Three.js (no equivalent parameter) |
| 🔄 | Supported but with different semantic interpretation |

---

## 1. Base Layer Properties

The base layer defines the fundamental appearance of the material - its color, reflectivity, and surface texture.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `base_weight` | `inputs:base_weight` | float | 1.0 | ⚠️ | `opacity` | Affects overall opacity in Three.js; not a direct base layer weight |
| `base_color` | `inputs:base_color` | color3f | (0.8, 0.8, 0.8) | ✅ | `color` | Direct 1:1 mapping to diffuse color |
| `base_roughness` | `inputs:base_roughness` | float | 0.0 | ✅ | `roughness` | Direct mapping for microfacet roughness |
| `base_metalness` | `inputs:base_metalness` | float | 0.0 | ✅ | `metalness` | Direct mapping for metallic workflow |

**Three.js Notes:**
- `base_weight` doesn't have a direct equivalent; Three.js uses `opacity` for transparency
- Base layer is the foundation of the PBR material in both OpenPBR and Three.js

---

## 2. Specular Reflection Properties

Specular properties control the shiny, mirror-like reflections on dielectric (non-metallic) surfaces.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `specular_weight` | `inputs:specular_weight` | float | 1.0 | ⚠️ | `reflectivity` (r170+) | Three.js r170+ has limited support |
| `specular_color` | `inputs:specular_color` | color3f | (1.0, 1.0, 1.0) | ⚠️ | `specularColor` (limited) | Only available in certain material types |
| `specular_roughness` | `inputs:specular_roughness` | float | 0.3 | ✅ | `roughness` | Same as base_roughness in Three.js |
| `specular_ior` | `inputs:specular_ior` | float | 1.5 | ✅ | `ior` | Index of refraction, directly supported |
| `specular_ior_level` | `inputs:specular_ior_level` | float | 0.5 | ❌ | — | No equivalent in Three.js |
| `specular_anisotropy` | `inputs:specular_anisotropy` | float | 0.0 | ⚠️ | `anisotropy` (r170+) | Experimental support in recent Three.js |
| `specular_rotation` | `inputs:specular_rotation` | float | 0.0 | ⚠️ | `anisotropyRotation` (r170+) | Requires anisotropy support |

**Three.js Notes:**
- Anisotropic reflection is experimental and not widely supported
- `specular_ior_level` has no Three.js equivalent
- Most specular properties require custom shader implementations for full fidelity

---

## 3. Transmission Properties (Transparency/Glass)

Transmission properties control light passing through the material, used for glass, water, and translucent materials.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `transmission_weight` | `inputs:transmission_weight` | float | 0.0 | ✅ | `transmission` | Supported in MeshPhysicalMaterial |
| `transmission_color` | `inputs:transmission_color` | color3f | (1.0, 1.0, 1.0) | ❌ | — | **NOT SUPPORTED** - Three.js uses white |
| `transmission_depth` | `inputs:transmission_depth` | float | 0.0 | ⚠️ | `thickness` | Approximate mapping, different semantics |
| `transmission_scatter` | `inputs:transmission_scatter` | color3f | (0.0, 0.0, 0.0) | ❌ | — | **NOT SUPPORTED** - Volume scattering |
| `transmission_scatter_anisotropy` | `inputs:transmission_scatter_anisotropy` | float | 0.0 | ❌ | — | **NOT SUPPORTED** - Advanced scattering |
| `transmission_dispersion` | `inputs:transmission_dispersion` | float | 0.0 | ❌ | — | **NOT SUPPORTED** - Chromatic dispersion |

**Three.js Notes:**
- ❌ **Transmission is NOT fully supported** - Only basic `transmission` weight is available
- ❌ Colored transmission, volume scattering, and dispersion require custom shaders
- Glass materials will appear simplified compared to OpenPBR specification

---

## 4. Subsurface Scattering Properties

Subsurface scattering simulates light penetrating and scattering beneath the surface, crucial for skin, wax, marble, etc.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `subsurface_weight` | `inputs:subsurface_weight` | float | 0.0 | ❌ | — | **NOT SUPPORTED** |
| `subsurface_color` | `inputs:subsurface_color` | color3f | (0.8, 0.8, 0.8) | ❌ | — | **NOT SUPPORTED** |
| `subsurface_radius` | `inputs:subsurface_radius` | float | 1.0 | ❌ | — | **NOT SUPPORTED** |
| `subsurface_radius_scale` | `inputs:subsurface_radius_scale` | color3f | (1.0, 1.0, 1.0) | ❌ | — | **NOT SUPPORTED** |
| `subsurface_scale` | `inputs:subsurface_scale` | float | 1.0 | ❌ | — | **NOT SUPPORTED** |
| `subsurface_anisotropy` | `inputs:subsurface_anisotropy` | float | 0.0 | ❌ | — | **NOT SUPPORTED** |

**Three.js Notes:**
- ❌ **Subsurface scattering is NOT supported in standard Three.js materials**
- Requires custom shader implementations (e.g., via shader chunks)
- Materials with SSS will fall back to standard diffuse appearance
- Community solutions exist but are not part of core Three.js

---

## 5. Sheen Properties (Fabric/Velvet)

Sheen adds a soft, velvet-like reflective layer, commonly used for cloth, fabric, and microfiber materials.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `sheen_weight` | `inputs:sheen_weight` | float | 0.0 | ✅ | `sheen` | Supported in MeshPhysicalMaterial |
| `sheen_color` | `inputs:sheen_color` | color3f | (1.0, 1.0, 1.0) | ✅ | `sheenColor` | Directly supported |
| `sheen_roughness` | `inputs:sheen_roughness` | float | 0.3 | ✅ | `sheenRoughness` | Directly supported |

**Three.js Notes:**
- ✅ Sheen is well supported in Three.js MeshPhysicalMaterial
- Good for fabric and cloth materials

---

## 6. Coat Layer Properties (Clear Coat)

Coat layer simulates a clear protective coating on top of the base material, like car paint or lacquered wood.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `coat_weight` | `inputs:coat_weight` | float | 0.0 | ✅ | `clearcoat` | Direct mapping |
| `coat_color` | `inputs:coat_color` | color3f | (1.0, 1.0, 1.0) | ❌ | — | **NOT SUPPORTED** - Three.js clearcoat is always white |
| `coat_roughness` | `inputs:coat_roughness` | float | 0.0 | ✅ | `clearcoatRoughness` | Direct mapping |
| `coat_anisotropy` | `inputs:coat_anisotropy` | float | 0.0 | ❌ | — | **NOT SUPPORTED** |
| `coat_rotation` | `inputs:coat_rotation` | float | 0.0 | ❌ | — | **NOT SUPPORTED** |
| `coat_ior` | `inputs:coat_ior` | float | 1.5 | ⚠️ | `ior` | Uses same IOR as base material |
| `coat_affect_color` | `inputs:coat_affect_color` | color3f | (1.0, 1.0, 1.0) | ❌ | — | **NOT SUPPORTED** |
| `coat_affect_roughness` | `inputs:coat_affect_roughness` | float | 0.0 | ❌ | — | **NOT SUPPORTED** |

**Three.js Notes:**
- ⚠️ Clear coat support is basic - only weight and roughness
- ❌ Colored clear coats not supported
- ❌ Anisotropic coat reflections not supported
- ❌ Advanced coat interactions (affect_color, affect_roughness) not supported

---

## 7. Emission Properties (Glow/Light)

Emission makes materials glow and emit light, used for light sources, LEDs, neon signs, etc.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `emission_luminance` | `inputs:emission_luminance` | float | 0.0 | ✅ | `emissiveIntensity` | Direct mapping |
| `emission_color` | `inputs:emission_color` | color3f | (1.0, 1.0, 1.0) | ✅ | `emissive` | Direct mapping |

**Three.js Notes:**
- ✅ Emission is fully supported
- Works well for glowing materials and light-emitting surfaces

---

## 8. Geometry Properties

Geometry properties affect surface normals and tangent space, used for bump mapping, normal mapping, etc.

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Default | Three.js Support | Three.js Mapping | Notes |
|-------------------|------------------------|------|---------|------------------|------------------|-------|
| `opacity` | `inputs:opacity` | float | 1.0 | ✅ | `opacity` + `transparent` flag | Direct mapping |
| `normal` | `inputs:normal` | normal3f | (0.0, 0.0, 1.0) | ✅ | `normalMap` | Normal map support |
| `tangent` | `inputs:tangent` | vector3f | (1.0, 0.0, 0.0) | ⚠️ | Computed internally | Three.js computes tangents automatically |

**Three.js Notes:**
- ✅ Opacity and normal mapping fully supported
- Tangent vectors are usually computed by Three.js from geometry

---

## 9. Outputs

| TinyUSDZ Parameter | Blender/MaterialX Name | Type | Three.js Support | Notes |
|-------------------|------------------------|------|------------------|-------|
| `surface` | `token outputs:surface` | token | ✅ | Output connection for shader graph |

---

## Summary Statistics

### Support Overview

| Category | Parameters | Fully Supported | Partially Supported | Not Supported |
|----------|-----------|-----------------|---------------------|---------------|
| Base Layer | 4 | 3 | 1 | 0 |
| Specular | 7 | 2 | 3 | 2 |
| Transmission | 6 | 1 | 1 | 4 |
| Subsurface | 6 | 0 | 0 | 6 |
| Sheen | 3 | 3 | 0 | 0 |
| Coat | 8 | 2 | 1 | 5 |
| Emission | 2 | 2 | 0 | 0 |
| Geometry | 3 | 2 | 1 | 0 |
| **Total** | **39** | **15 (38%)** | **7 (18%)** | **17 (44%)** |

### Critical Limitations for Three.js

**❌ NOT SUPPORTED (requires custom shaders):**
1. **Subsurface Scattering** - All 6 parameters (weight, color, radius, radius_scale, scale, anisotropy)
2. **Transmission Effects** - Color, scatter, dispersion (4 parameters)
3. **Coat Advanced** - Color, anisotropy, affect properties (5 parameters)
4. **Specular Advanced** - IOR level (1 parameter)

**⚠️ LIMITATIONS:**
- Transmission is basic (only weight supported, no colored transmission)
- Coat layer cannot be colored or anisotropic
- Anisotropic reflections are experimental
- No volume scattering or participating media

**✅ WELL SUPPORTED:**
- Base PBR (color, metalness, roughness)
- Emission (color and intensity)
- Sheen (fabric materials)
- Basic clear coat
- Normal mapping and opacity

---

## Blender MaterialX Export Format

When Blender v4.5+ exports OpenPBR materials to MaterialX, it uses this structure:

```xml
<materialx version="1.38" colorspace="lin_rec709">
  <material name="MaterialName">
    <shaderref name="SR_OpenPBRSurface" node="OpenPBRSurface">
      <!-- Parameter bindings -->
      <bindinput name="base_color" type="color3" value="0.8, 0.8, 0.8" />
      <bindinput name="base_metalness" type="float" value="0.0" />
      <bindinput name="base_roughness" type="float" value="0.5" />
      <!-- Texture connections -->
      <bindinput name="base_color" type="color3" nodename="image_basecolor" />
    </shaderref>
  </material>

  <image name="image_basecolor" type="color3">
    <input name="file" type="filename" value="textures/base_color.png" />
  </image>

  <open_pbr_surface name="OpenPBRSurface" type="surfaceshader">
    <!-- Full parameter list -->
  </open_pbr_surface>
</materialx>
```

### USD Format

In USD files, OpenPBR materials appear as:

```usda
def Material "MaterialName"
{
    token outputs:surface.connect = </Materials/MaterialName/OpenPBRSurface.outputs:surface>

    def Shader "OpenPBRSurface"
    {
        uniform token info:id = "OpenPBRSurface"
        # Or: uniform token info:id = "ND_open_pbr_surface_surfaceshader"

        color3f inputs:base_color = (0.8, 0.8, 0.8)
        float inputs:base_metalness = 0.0
        float inputs:base_roughness = 0.5
        # ... all other inputs

        token outputs:surface
    }
}
```

---

## Conversion Recommendations

### For Three.js WebGL Target

When converting OpenPBR to Three.js MeshPhysicalMaterial:

1. **Use Supported Parameters:**
   - Base color, metalness, roughness → Direct mapping
   - Emission color and luminance → Direct mapping
   - Sheen weight, color, roughness → Direct mapping
   - Clearcoat weight and roughness → Direct mapping
   - Transmission weight → Basic transparency

2. **Handle Unsupported Parameters:**
   - **Subsurface Scattering**: Approximate with albedo color darkening
   - **Transmission Color**: Warn user, fall back to white
   - **Coat Color**: Warn user, fall back to white clearcoat
   - **Advanced Specular**: Use base `ior` parameter, ignore `specular_ior_level`

3. **Texture Handling:**
   - Map `inputs:base_color` texture → `map`
   - Map `inputs:base_metalness` texture → `metalnessMap`
   - Map `inputs:base_roughness` texture → `roughnessMap`
   - Map `inputs:emission_color` texture → `emissiveMap`
   - Map `inputs:normal` texture → `normalMap`

### For Three.js WebGPU Target

With WebGPU and MaterialX node support:
- More parameters may become available
- Custom node implementations can handle advanced features
- Refer to Three.js MaterialXLoader documentation

---

## References

- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
- [MaterialX Specification v1.38](https://www.materialx.org/)
- [Three.js MeshPhysicalMaterial Documentation](https://threejs.org/docs/#api/en/materials/MeshPhysicalMaterial)
- [TinyUSDZ OpenPBR Implementation](../src/usdShade.hh)
- [Three.js MaterialX Loader](https://threejs.org/examples/webgpu_loader_materialx.html)

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2025-11-06 | 1.0 | Initial comprehensive parameter reference |

