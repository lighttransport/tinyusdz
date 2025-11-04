# OpenPBR MaterialX Test Files

This directory contains synthetic USDA test files demonstrating OpenPBR materials with textures.

## Test Files

### Basic Material Tests

1. **openpbr-brick-sphere.usda** - Simple diffuse material with brick texture
   - OpenPBRSurface with base_color from texture
   - Low metalness (0.0), medium roughness (0.7)
   - Uses: textures/brick.bmp

2. **openpbr-metallic-cube.usda** - Metallic material with checkerboard texture
   - High metalness (0.8), low roughness (0.2)
   - Specular IOR: 1.5
   - Uses: textures/checkerboard.png

3. **openpbr-emissive-plane.usda** - Emissive material with cat texture
   - Emission from texture (emission_luminance: 10.0)
   - Low base weight (0.5)
   - Uses: textures/texture-cat.jpg
   - **Note**: Uses explicit Mesh - material detectable by Tydra

4. **openpbr-glass-sphere.usda** - Glass/transmission material
   - Zero base weight, full transmission (0.95)
   - Zero roughness for clear glass
   - Transmission color: slight blue tint

5. **openpbr-subsurface-sphere.usda** - Subsurface scattering material
   - Base color and subsurface color from texture
   - Subsurface weight: 0.5, radius: 0.1
   - Radius scale: (1.0, 0.5, 0.3) for realistic SSS
   - Uses: textures/01.jpg

6. **openpbr-coated-cube.usda** - Clear coat material
   - Brick texture base with clear coat layer
   - Coat weight: 0.8, coat roughness: 0.1
   - Coat IOR: 1.5
   - Uses: textures/brick.bmp

### Multi-Object Scene

7. **openpbr-multi-object.usda** - Scene with multiple objects and materials
   - BrickSphere: Rough brick material
   - MetalCube: Smooth gold-colored metal (no texture)
   - GlassSphere: Clear glass with transmission
   - GroundPlane: Checkerboard pattern (5x tiled)
   - **Note**: Only GroundPlane material (CheckerMaterial) is detectable by Tydra as it's an explicit Mesh

## Texture Files

All test files reference textures in the `textures/` subdirectory:

- **brick.bmp** - 64x64 red brick pattern with gray mortar (generated)
- **checkerboard.png** - Black and white checkerboard pattern
- **texture-cat.jpg** - Cat photo texture
- **01.jpg** - Color texture for subsurface scattering

## Material Structure

All materials follow this structure:

```
def Scope "_materials"
{
    def Material "MaterialName"
    {
        token outputs:surface.connect = <path/to/OpenPBRSurface.outputs:surface>

        def Shader "OpenPBRSurface"
        {
            uniform token info:id = "OpenPBRSurface"
            // OpenPBR parameters (inputs:base_color, etc.)
            token outputs:surface
        }

        def Shader "TextureName" (optional)
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @./textures/filename@
            float2 inputs:texcoord.connect = <path/to/Primvar.outputs:result>
            color3f outputs:rgb
        }

        def Shader "Primvar" (optional)
        {
            uniform token info:id = "UsdPrimvarReader_float2"
            int inputs:index = 0
            float2 outputs:result
        }
    }
}
```

## Shader Node IDs

- **OpenPBRSurface**: OpenPBR surface shader (info:id = "OpenPBRSurface")
- **UsdUVTexture**: Texture sampler (info:id = "UsdUVTexture")
- **UsdPrimvarReader_float2**: UV coordinate reader (info:id = "UsdPrimvarReader_float2")

**Note**: MaterialX node definition IDs (e.g., "ND_open_pbr_surface_surfaceshader") are not currently supported. Use the simplified USD shader IDs above.

## Tydra RenderScene Conversion Notes

The Tydra render converter currently only detects materials bound to **explicit Mesh primitives**. Parametric primitives (Sphere, Cube) are not converted, so their materials won't appear in RenderScene.

**Files with detectable materials:**
- openpbr-emissive-plane.usda (uses Mesh)
- openpbr-multi-object.usda (only GroundPlane/CheckerMaterial detected)

**Files with non-detectable materials (parametric primitives):**
- openpbr-brick-sphere.usda
- openpbr-metallic-cube.usda
- openpbr-glass-sphere.usda
- openpbr-subsurface-sphere.usda
- openpbr-coated-cube.usda

These files still parse correctly and can be used for testing MaterialX parsing, but won't export materials via the WASM MaterialX dumper CLI.

## Testing

### Native Build
```bash
# Parse test
./build/tusdcat models/openpbr-brick-sphere.usda

# Note: OpenPBRSurface may show as "[???] Invalid ShaderNode" in output
# This is a pprinter limitation, not a parsing error
```

### WASM MaterialX Export
```bash
cd web/js
npm run dump-materialx -- ../../models/openpbr-emissive-plane.usda -v
npm run dump-materialx -- ../../models/openpbr-multi-object.usda -f json
```

## OpenPBR Parameters Demonstrated

- **Base Layer**: base_weight, base_color, base_metalness, base_roughness
- **Specular**: specular_weight, specular_roughness, specular_ior
- **Transmission**: transmission_color, transmission_weight
- **Emission**: emission_color, emission_luminance
- **Subsurface**: subsurface_color, subsurface_weight, subsurface_radius, subsurface_radius_scale
- **Coat**: coat_weight, coat_roughness, coat_ior, coat_color
- **Geometry**: geometry_thin_walled (for glass)

## Related Files

- `create_brick_texture.py` - Python script to generate brick.bmp texture
- `polysphere-materialx-001.usda` - More complex MaterialX scene from Blender
