# MaterialX Light Shader Test Examples

This directory contains USDA test files demonstrating MaterialX light shader integration with TinyUSDZ.

## Files

### 01_basic_uniform_light.usda
**Basic Omnidirectional Point Light**

Demonstrates:
- `uniform_edf` - Emits light uniformly in all directions
- MaterialX `light` shader node combining EDF with intensity
- USD SphereLight equivalent representation
- Simple material binding

**Key Features:**
- Warm white light (color temperature simulation)
- Intensity and exposure controls
- Maps to USD SphereLight

---

### 02_conical_spotlight.usda
**Conical Spotlight with Soft Edges**

Demonstrates:
- `conical_edf` - Cone-shaped emission for spotlights
- Inner/outer angle controls for soft-edge spotlights
- MaterialX spotlight → USD RectLight + ShapingAPI mapping

**Key Features:**
- Inner cone angle: 30°
- Outer cone angle: 45° (creates soft falloff)
- ShapingAPI cone control
- Glossy metallic material for specular highlights

**Technical Notes:**
- `conical_edf` inner_angle maps to ShapingAPI cone:angle
- Outer angle creates softness via (outer - inner) / inner calculation
- Normal vector controls emission direction

---

### 03_measured_ies_light.usda
**IES Profile-Based Realistic Lighting**

Demonstrates:
- `measured_edf` - Real-world light distribution from IES profiles
- IES file integration via ShapingAPI
- Proper warm light color with IES pattern

**Key Features:**
- IES profile file reference
- IES normalization for consistent brightness
- Gold metallic material showcasing realistic light interaction
- Floor geometry for light pattern visualization

**Technical Notes:**
- IES files define real photometric light distributions
- MaterialX `file` input maps to ShapingAPI ies:file
- Normalization ensures consistent intensity across profiles

---

### 04_complete_scene.usda
**Three-Point Lighting Setup with Multiple Materials**

Demonstrates:
- Complete production lighting setup
- Multiple MaterialX EDF types in one scene
- Advanced material library (metallic, dielectric, diffuse, concrete)
- Professional lighting techniques

**Lighting Setup:**

1. **Key Light** (uniform_edf)
   - Primary illumination source
   - Warm white (5500K color temperature)
   - High intensity (8.0) with +1 EV exposure
   - Position: Front-right, elevated

2. **Fill Light** (conical_edf)
   - Reduces harsh shadows from key light
   - Cool white (7000K) for color variation
   - Wide cone (60°) for broad coverage
   - Lower intensity (3.0)
   - Position: Front-left, elevated

3. **Back Light** (conical_edf)
   - Rim lighting for edge definition
   - Tight spotlight (25° cone)
   - Higher intensity (6.0) for prominence
   - ShadowAPI enabled for control
   - Position: Behind and above

4. **Environment Light** (DomeLight)
   - Ambient fill from sky
   - Low intensity (0.3) with -1 EV
   - Sky blue tint

**Materials:**

- **Concrete** - Rough, non-metallic floor (roughness: 0.9)
- **Metallic** - Gold sphere (metallic: 1.0, roughness: 0.2)
- **Dielectric** - Blue glass cube (transparent, IOR: 1.5)
- **Diffuse** - Red matte cylinder (roughness: 0.6)

---

### 05_mtlx_reference.usda
**MaterialX File Reference Example**

Demonstrates:
- Referencing external .mtlx files from USD
- USD reference composition with MaterialX
- Overriding MaterialX light parameters
- Multiple light shader references

**Key Features:**
- External MaterialX file (`example_light.mtlx`)
- Reference syntax: `prepend references = @./example_light.mtlx@</main_light>`
- Parameter overrides using USD's opinion strength
- Shows all three EDF types via references

**Technical Notes:**
- MaterialX files can be referenced like any USD layer
- USD path syntax `</main_light>` targets specific light in .mtlx
- Overrides in USD take precedence over MaterialX defaults
- Enables sharing MaterialX definitions across multiple USD files

---

### example_light.mtlx
**Pure MaterialX XML Light Definitions**

A standalone MaterialX 1.38 file containing:
- Three EDF definitions (uniform, conical, measured)
- Three light shader definitions
- Proper MaterialX XML structure
- Can be referenced from USD files

This file demonstrates the MaterialX XML format that TinyUSDZ can parse.

---

## MaterialX Light Shader Concepts

### Emission Distribution Functions (EDFs)

MaterialX separates light definition into:
1. **EDF Node** - Defines emission pattern (uniform, conical, measured)
2. **Light Shader** - Combines EDF with intensity/exposure controls

This modular approach allows reusing EDFs across multiple lights.

### USD Mapping

| MaterialX EDF | USD Light Type | Additional APIs |
|---------------|----------------|-----------------|
| uniform_edf   | SphereLight    | - |
| conical_edf   | RectLight      | ShapingAPI (cone) |
| measured_edf  | SphereLight    | ShapingAPI (IES) |

### Color Temperature

While MaterialX uses direct color3f values, USD supports color temperature:
- `enableColorTemperature` - Enables blackbody radiation calculation
- `colorTemperature` - Temperature in Kelvin (2700K-10000K typical range)

Common values:
- 2700K - Warm incandescent
- 3200K - Tungsten
- 5500K - Daylight
- 6500K - Cool daylight
- 7000K - Overcast sky

### Exposure Value (EV)

Exposure control using photographic EV stops:
- EV 0.0 = baseline intensity
- EV +1.0 = 2× brighter
- EV -1.0 = 0.5× dimmer
- Formula: final_intensity = base_intensity × 2^exposure

---

## Testing These Files

### With TinyUSDZ

```bash
# Parse and validate
tinyusdz_viewer 01_basic_uniform_light.usda

# Test MaterialX parsing
tinyusdz_test --mtlx 04_complete_scene.usda
```

### Expected Behavior

1. **Light Parsing**
   - MaterialX light shaders should be recognized
   - EDF nodes correctly parsed with parameters
   - Light shader connections validated

2. **Conversion**
   - MaterialX lights convert to appropriate USD light types
   - Intensity and color properly transferred
   - ShapingAPI applied for conical/measured EDFs

3. **Rendering**
   - Three-point lighting creates proper illumination
   - Materials respond correctly to light properties
   - Shadows and specular highlights visible

---

## Notes

- IES file paths in `03_measured_ies_light.usda` are placeholder references
- For actual rendering, replace with valid IES profile files
- Color temperature is simulated via RGB values in pure MaterialX
- USD's native color temperature is used in USD light representations

---

### 06_mesh_lights.usda
**MeshLightAPI Test Cases**

Demonstrates:
- `MeshLightAPI` - Converting geometry into area lights
- Multiple mesh types as emissive lights
- `materialSyncMode` parameter variants
- Different mesh light shapes (cube, rectangle, torus, disk)

**Test Scenarios:**

1. **EmissiveSphere** - Simple emissive cube mesh
   - Basic MeshLightAPI application
   - Intensity: 100, orange color
   - Normalize enabled for physical correctness
   - Orange emissive material

2. **EmissiveRectangle** - Rectangle area light
   - `materialSyncMode: materialGlowTintsLight`
   - Material emission color affects light output
   - Green tinted light synchronized with material
   - Positioned overhead for general illumination

3. **EmissiveTorus** - Complex multi-faced mesh light
   - `materialSyncMode: independent`
   - Light color independent of material emission
   - Red light output despite blue material
   - Demonstrates complex geometry as light source

4. **EmissiveDisk** - Circular disk light (triangulated)
   - Color temperature enabled (6500K daylight)
   - Multiple triangular faces forming disk
   - White emissive material
   - Normalize enabled

**Key Features:**
- Material binding to emissive materials
- Double-sided vs single-sided emission
- Different materialSyncMode behaviors
- Various mesh topologies as light sources

---

### 07_animated_mesh_lights.usda
**Animated MeshLightAPI with Time Samples**

Demonstrates:
- Time-sampled light properties
- Animated transforms for moving lights
- Color animation and pulsing effects
- Color temperature animation

**Test Scenarios:**

1. **PulsingSphere** - Intensity animation
   - Pulsing effect: 10 → 500 → 10 intensity over time
   - Time samples at frames 1, 30, 60, 90, 120
   - Orange-red color
   - Demonstrates dramatic intensity changes

2. **ColorChangingRect** - Color animation
   - Color cycling: Red → Yellow → Green → Cyan → Red
   - Smooth color transitions using time samples
   - `materialSyncMode: independent` for controlled color
   - Fixed intensity, animated hue

3. **OrbitingLight** - Transform animation
   - Circular orbital motion
   - Animated translate transform
   - Fixed light properties, moving position
   - Blue colored light

4. **ColorTempLight** - Color temperature animation
   - Animates between 2700K (warm) and 6500K (cool)
   - Demonstrates white balance shifts
   - Warm incandescent → neutral → cool daylight cycle
   - Color temperature physically drives color output

**Key Features:**
- `.timeSamples` syntax for property animation
- Multiple properties can be animated independently
- Transform animation vs property animation
- Timeline: 120 frames at 24 fps (5 seconds)

**Technical Notes:**
- Color temperature overrides explicit color when enabled
- Time samples interpolate linearly between keyframes
- Transform animation moves entire mesh and its emission
- Material properties can also be time-sampled

---

## MeshLightAPI Concepts

### Geometry as Light Source

MeshLightAPI converts any mesh geometry into an area light:
- Each face emits light according to its area
- Supports complex shapes and topologies
- More physically accurate than point/directional lights
- Computationally expensive for many faces

### materialSyncMode

Controls interaction between material emission and light output:

| Mode | Behavior |
|------|----------|
| `materialGlowTintsLight` | Material emission color multiplies with light color |
| `independent` | Light color is independent of material |
| `noMaterialResponse` | Mesh doesn't receive lighting from other sources |

### When to Use Mesh Lights

**Advantages:**
- Physically accurate soft shadows
- Natural light falloff for area sources
- Realistic illumination for large emissive surfaces
- Supports complex custom shapes

**Disadvantages:**
- Higher computational cost than analytic lights
- Requires tessellated geometry (more faces = more cost)
- May need many samples for noise-free rendering

**Best For:**
- Light panels and strips
- Emissive screens and displays
- Neon signs and lit signage
- Architectural lighting (windows, ceiling panels)
- Sci-fi glowing elements

---

## Testing These Files

### With TinyUSDZ

```bash
# Parse and validate mesh lights
tinyusdz_viewer 06_mesh_lights.usda

# Test animated mesh lights
tinyusdz_viewer 07_animated_mesh_lights.usda --frame 60

# Convert to render scene
tinyusdz_test --tydra 06_mesh_lights.usda

# Export to Three.js
tinyusdz_export --threejs 07_animated_mesh_lights.usda -o output.json
```

### Expected Behavior

1. **Light Parsing**
   - MeshLightAPI schema detected on mesh prims
   - Light properties extracted (intensity, color, normalize, etc.)
   - materialSyncMode correctly parsed
   - Time-sampled properties handled

2. **Conversion to RenderScene**
   - Meshes with MeshLightAPI create RenderLight entries
   - RenderLight.lightType = Geometry
   - geometry_mesh_id references the mesh
   - material_sync_mode stored for renderer

3. **JSON Serialization**
   - Light exports as JSON with type "MeshEmissive"
   - Includes mesh reference via geometry_mesh_id
   - Material sync mode included in userData
   - Time-sampled values exported at current timecode

4. **Three.js Export**
   - Geometry lights exported with mesh references
   - Emissive material properly set
   - Animation tracks created for time-sampled properties
   - userData contains UsdLux-specific parameters

---

## References

- [MaterialX Specification v1.38](https://materialx.org/assets/MaterialX.v1.38D1.Spec.pdf)
- [MaterialX PBR Spec](https://materialx.org/assets/MaterialX.v1.38.PBRSpec.pdf)
- [USD Lux Schema](https://openusd.org/release/api/usd_lux_page_front.html)
- [USD MeshLightAPI](https://openusd.org/release/api/class_usd_lux_mesh_light_a_p_i.html)
- [TinyUSDZ Documentation](../../../README.md)
