# PBR Debugging Enhancements for MaterialX Web Demo

Comprehensive proposal for enhanced debugging features for Three.js MeshPhysicalMaterial PBR shading in the TinyUSDZ MaterialX web demo.

## Current State Analysis

**Already Implemented** ✅:
- AOV (Arbitrary Output Variable) visualization modes for normals, tangents, UVs, depth, albedo, roughness, metalness, specular, coat, transmission, emissive
- Color Picker (samples final rendered output after lighting)
- Material Property Picker (samples material properties before lighting)
- Material JSON Viewer (inspect material parameters)
- Node Graph Viewer (visualize MaterialX networks)
- False color tone mapping visualization
- Texture panel with colorspace controls

**Gaps Identified**:
- No real-time shader output visualization
- Limited texture channel debugging
- No lighting/IBL contribution analysis
- No material comparison tools
- Limited validation/error detection
- No performance profiling for materials

---

## Category 1: Enhanced Material Property Visualization

### 1.1 Real-Time G-Buffer Viewer

**Purpose**: Visualize all material properties simultaneously in a multi-panel view

**Implementation**:
- Create 2×2 or 3×3 grid view showing multiple AOVs at once
- Display: Albedo | Normal | Roughness | Metalness in one view
- Allow drag-to-rearrange panels
- Save/load layouts

**UI**:
```
┌─────────┬─────────┬─────────┐
│ Albedo  │ Normal  │ Rough   │
├─────────┼─────────┼─────────┤
│ Metal   │ Emission│ AO      │
├─────────┼─────────┼─────────┤
│ Coat    │ Trans   │ Depth   │
└─────────┴─────────┴─────────┘
```

**Benefits**:
- See all material channels at once
- Quickly identify inconsistencies
- Compare texture channels side-by-side

**Effort**: Medium (requires render target management, UI layout)

---

### 1.2 Advanced AOV Modes

**Purpose**: Add missing critical AOV channels

**New AOV Modes**:

#### 1.2.1 Ambient Occlusion
```glsl
// Sample from aoMap (usually in red channel)
float ao = texture2D(aoMap, vUv).r;
gl_FragColor = vec4(vec3(ao), 1.0);
```

**Use case**: Verify AO map loading, check intensity

#### 1.2.2 Anisotropy
```glsl
// Visualize anisotropy direction and strength
vec2 aniso = texture2D(anisotropyMap, vUv).rg;
vec3 color = vec3(aniso * 0.5 + 0.5, anisotropyStrength);
gl_FragColor = vec4(color, 1.0);
```

**Use case**: Debug brushed metal, hair materials

#### 1.2.3 Thickness (for transmission)
```glsl
float thickness = texture2D(thicknessMap, vUv).g;
gl_FragColor = vec4(vec3(thickness), 1.0);
```

**Use case**: Debug subsurface scattering, thin glass

#### 1.2.4 Sheen
```glsl
vec3 sheenColor = texture2D(sheenColorMap, vUv).rgb;
float sheenRoughness = texture2D(sheenRoughnessMap, vUv).a;
gl_FragColor = vec4(sheenColor, sheenRoughness);
```

**Use case**: Debug fabric materials (velvet, cloth)

#### 1.2.5 Iridescence
```glsl
float iridescence = texture2D(iridescenceMap, vUv).r;
float iridescenceIOR = iridescenceIORValue;
float thickness = texture2D(iridescenceThicknessMap, vUv).g;
gl_FragColor = vec4(iridescence, iridescenceIOR, thickness, 1.0);
```

**Use case**: Debug soap bubbles, oil slicks, butterfly wings

#### 1.2.6 Normal Map Quality Check
```glsl
// Visualize normal map in tangent space with error detection
vec3 normalMap = texture2D(normalMap, vUv).rgb * 2.0 - 1.0;
float length = length(normalMap);

// Red if invalid (length too far from 1.0)
float error = abs(length - 1.0);
vec3 color = error > 0.1 ? vec3(1.0, 0.0, 0.0) : normalMap * 0.5 + 0.5;

gl_FragColor = vec4(color, 1.0);
```

**Use case**: Detect corrupted/invalid normal maps

#### 1.2.7 Specular F0 (Fresnel at 0°)
```glsl
// Calculate and visualize F0 based on IOR
float ior = iorValue;
float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);

// Or from specular intensity/color
vec3 f0Color = specularColor * specularIntensity;

gl_FragColor = vec4(f0Color, 1.0);
```

**Use case**: Verify IOR-based reflectance calculations

**Effort**: Low-Medium (shader-based, similar to existing AOVs)

---

### 1.3 Texture Channel Inspector

**Purpose**: Deep-dive into individual texture channels with histogram and statistics

**Features**:
- Click on texture to open inspector
- Show R, G, B, A channels separately
- Display histogram (value distribution)
- Show min/max/average values
- Detect issues:
  - All zeros (texture not loaded)
  - Clamped values (0.0 or 1.0 only)
  - Unexpected range (e.g., normals outside [-1,1])
  - Single color (texture is uniform)

**UI**:
```
┌─────────────────────────────┐
│ Base Color Map              │
├─────────────────────────────┤
│ [Texture Preview]           │
│                              │
│ Channel Statistics:          │
│ R: min=0.12 max=0.98 avg=0.54│
│ G: min=0.08 max=0.95 avg=0.48│
│ B: min=0.05 max=0.88 avg=0.42│
│ A: 1.0 (constant)            │
│                              │
│ [Histogram]                  │
│    ▂▄█▆▃▂▁                  │
│                              │
│ ⚠ Warning: High contrast     │
│ ✓ Valid sRGB range          │
└─────────────────────────────┘
```

**Implementation**:
- Read texture data from GPU using `readPixels()`
- Calculate statistics in web worker (async)
- Draw histogram using Canvas2D

**Benefits**:
- Quickly identify texture loading issues
- Verify texture value ranges
- Detect compression artifacts

**Effort**: Medium-High (requires texture readback, statistics computation, UI)

---

## Category 2: Lighting & Environment Debugging

### 2.1 IBL Contribution Analyzer

**Purpose**: Visualize how much environment lighting contributes to final color

**Features**:
- Split-screen comparison: With IBL vs. Without IBL
- Difference view (IBL contribution only)
- Per-channel contribution (diffuse vs. specular IBL)
- Real-time intensity adjustment with live preview

**Implementation**:
```javascript
// Render 3 passes:
// Pass 1: Full lighting (IBL + direct lights)
// Pass 2: Direct lights only (envMapIntensity = 0)
// Pass 3: Difference (Pass 1 - Pass 2)

// Show IBL contribution as heatmap
const contribution = (withIBL - withoutIBL) / withIBL;
```

**UI Controls**:
- `[×] Show IBL Diffuse Only`
- `[×] Show IBL Specular Only`
- `[ ] Show IBL Total`
- `Intensity: [====|====] 1.0`

**Benefits**:
- Understand environment map impact
- Optimize IBL intensity for desired look
- Debug overly bright/dark materials

**Effort**: Medium (requires multi-pass rendering, material cloning)

---

### 2.2 Light Probe Visualizer

**Purpose**: Visualize environment map sampling direction and intensity

**Features**:
- Overlay sampling vectors on 3D view
- Show where surface samples environment map
- Color-code by intensity (red=high, blue=low)
- Visualize reflection vector for specular

**Implementation**:
```glsl
// Calculate view-dependent reflection vector
vec3 viewDir = normalize(cameraPosition - vWorldPosition);
vec3 normal = normalize(vNormal);
vec3 reflectVec = reflect(-viewDir, normal);

// Sample environment map at reflection direction
vec3 envColor = textureCube(envMap, reflectVec).rgb;

// Visualize as arrow/line overlay
```

**UI**:
- Toggle between diffuse (irradiance) and specular (radiance) probes
- Adjust vector length/density
- Filter by roughness (rough surfaces sample wider area)

**Benefits**:
- Understand why surfaces look certain way
- Debug reflection issues
- Visualize Fresnel effect

**Effort**: High (requires custom geometry for vectors, complex visualization)

---

### 2.3 BRDF Visualizer (Material Response Viewer)

**Purpose**: Show how material responds to light from different angles

**Features**:
- Real-time BRDF lobe visualization
- Shows specular/diffuse contribution by angle
- Interactive: Rotate light direction, see response
- Display Fresnel curve for current material

**Implementation**:
- Generate half-sphere of light directions
- Evaluate BRDF for each direction
- Render as 3D plot or 2D heatmap

**UI**:
```
        ↑ Normal
        │
    ●───┼───●  ← Light directions
   ●    │    ●
  ●─────┼─────●

Brightness = BRDF response
```

**Example Use Cases**:
- Verify roughness affects specular lobe size correctly
- Check metallic materials have colored reflections
- Ensure Fresnel falloff is correct

**Effort**: High (complex visualization, requires BRDF evaluation)

---

## Category 3: Texture & UV Debugging

### 3.1 UV Layout Overlay

**Purpose**: Overlay UV wireframe on textured surface

**Features**:
- Draw UV grid lines directly on 3D mesh
- Highlight UV seams (discontinuities)
- Color-code UV islands
- Show UV stretch/compression heatmap

**Implementation**:
```glsl
// Detect UV seams by checking derivatives
vec2 uvDx = dFdx(vUv);
vec2 uvDy = dFdy(vUv);
float seam = (length(uvDx) > threshold || length(uvDy) > threshold) ? 1.0 : 0.0;

// Draw grid lines
vec2 grid = fract(vUv * gridFrequency);
float gridLine = (grid.x < lineWidth || grid.y < lineWidth) ? 1.0 : 0.0;

// Mix with base color
gl_FragColor = mix(baseColor, vec4(1, 1, 0, 1), gridLine * 0.5);
```

**Benefits**:
- Identify UV layout issues
- Find texture stretching
- Detect UV seams causing visible artifacts

**Effort**: Low-Medium (shader-based overlay)

---

### 3.2 Mip-Map Level Visualizer

**Purpose**: Show which mip-map level is being sampled

**Features**:
- Color-code by mip level (level 0=red, level 1=orange, ..., level 5+=blue)
- Helps identify texture resolution issues
- Shows where more/less detail is needed

**Implementation**:
```glsl
// Calculate mip level based on UV derivatives
vec2 uvDx = dFdx(vUv * textureSize);
vec2 uvDy = dFdy(vUv * textureSize);
float delta = max(dot(uvDx, uvDx), dot(uvDy, uvDy));
float mipLevel = 0.5 * log2(delta);

// Color-code mip levels
vec3 mipColors[6] = vec3[](
    vec3(1,0,0), vec3(1,0.5,0), vec3(1,1,0),
    vec3(0,1,0), vec3(0,0.5,1), vec3(0,0,1)
);
int level = clamp(int(mipLevel), 0, 5);
gl_FragColor = vec4(mipColors[level], 1.0);
```

**Benefits**:
- Optimize texture resolution
- Identify aliasing issues
- Verify texture filtering settings

**Effort**: Low (shader-based)

---

### 3.3 Texture Tiling Detector

**Purpose**: Automatically detect repeating texture patterns

**Features**:
- Analyze texture for repetition
- Highlight tiling boundaries
- Suggest if texture needs variation (e.g., decals, noise overlay)

**Implementation**:
- Sample texture at multiple frequencies
- Detect periodic patterns using FFT or autocorrelation
- Highlight repetition in red overlay

**Benefits**:
- Improve visual quality (avoid obvious tiling)
- Identify need for texture bombing or variation

**Effort**: High (requires signal processing, GPU readback)

---

## Category 4: Material Comparison & Validation

### 4.1 Side-by-Side Material Comparison

**Purpose**: Compare two materials (or two versions of same material) in split-screen

**Features**:
- Load two USD files or two materials from same file
- Display side-by-side or with draggable divider
- Difference mode (highlight differences in red)
- Sync camera between views

**UI**:
```
┌──────────────┬──────────────┐
│  Material A  │  Material B  │
│  (Original)  │  (Modified)  │
│              │              │
│      🔴      │      🔵      │
│              │              │
└──────────────┴──────────────┘

Differences: Roughness (+0.15), Metalness (-0.05)
```

**Use Cases**:
- Compare MaterialX export with original Blender material
- Validate changes during material editing
- QA material conversions

**Effort**: Medium (requires dual scene rendering, diff computation)

---

### 4.2 Material Validation & Linting

**Purpose**: Automatically detect common material errors and best practices violations

**Validation Checks**:

#### Energy Conservation
```javascript
// Check if baseColor * metalness > 1.0 (physically incorrect)
if (baseColor.r * metalness > 1.0) {
    warnings.push("Base color too bright for metallic material");
}

// Check if specular + diffuse > 1.0
const diffuse = baseColor * (1 - metalness);
const specular = calculateSpecular(ior, roughness);
if (diffuse + specular > 1.0) {
    warnings.push("Material violates energy conservation");
}
```

#### IOR Validation
```javascript
// Check for physically plausible IOR values
if (ior < 1.0 || ior > 3.0) {
    warnings.push(`IOR ${ior} outside typical range [1.0-3.0]`);
}

// Warn about common IOR mistakes
if (Math.abs(ior - 1.5) < 0.01 && materialType === "metal") {
    warnings.push("Metallic material using dielectric IOR (1.5)");
}
```

#### Texture Compatibility
```javascript
// Check if texture dimensions are powers of 2
if (!isPowerOfTwo(texture.image.width) || !isPowerOfTwo(texture.image.height)) {
    warnings.push(`Texture ${texture.name} not power-of-2, may cause issues`);
}

// Check for missing normal map when roughness/metalness maps present
if ((roughnessMap || metalnessMap) && !normalMap) {
    suggestions.push("Consider adding normal map for more detail");
}
```

#### Color Space Validation
```javascript
// Warn if base color map not in sRGB
if (baseColorMap.encoding !== sRGBEncoding) {
    warnings.push("Base color map should use sRGB encoding");
}

// Warn if data textures (normal, roughness) use sRGB
if (normalMap.encoding === sRGBEncoding) {
    errors.push("Normal map incorrectly using sRGB encoding");
}
```

**UI**:
```
┌─────────────────────────────┐
│ Material Validation Report  │
├─────────────────────────────┤
│ ✓ 12 checks passed          │
│ ⚠ 3 warnings                │
│ ❌ 1 error                   │
│                              │
│ Errors:                      │
│ ❌ Normal map using sRGB     │
│    encoding (should be       │
│    Linear)                   │
│                              │
│ Warnings:                    │
│ ⚠ Base color very bright    │
│   for metallic surface       │
│ ⚠ IOR value (2.8) unusual   │
│   for plastic material       │
│ ⚠ No AO map found           │
│                              │
│ [Fix All Errors]             │
└─────────────────────────────┘
```

**Benefits**:
- Catch errors before rendering
- Enforce PBR best practices
- Educational (explains why something is wrong)

**Effort**: Medium (requires material analysis, rule engine)

---

### 4.3 Reference Material Library

**Purpose**: Compare current material with physically accurate reference materials

**Features**:
- Built-in library of measured PBR values:
  - Metals: Gold, Silver, Copper, Iron, Aluminum
  - Dielectrics: Plastic, Glass, Water, Wood, Concrete
  - Organics: Skin, Leather, Fabric
- Show reference values side-by-side with current material
- One-click apply reference values

**Reference Data Example**:
```javascript
const REFERENCE_MATERIALS = {
    gold: {
        baseColor: [1.0, 0.766, 0.336],
        metalness: 1.0,
        roughness: 0.2,
        ior: 0.47, // Complex IOR (real part)
        f0: [1.0, 0.71, 0.29]
    },
    plastic_glossy: {
        baseColor: [0.5, 0.5, 0.5],
        metalness: 0.0,
        roughness: 0.1,
        ior: 1.5
    },
    // ... more references
};
```

**UI**:
```
Reference Materials:
[ Gold ] [ Silver ] [ Copper ] [ Glass ] [ Plastic ]

Current:        Reference (Gold):
Base: 0.9,0.7,0.3  Base: 1.0,0.766,0.336 ← Apply
Metal: 0.85        Metal: 1.0            ← Apply
Rough: 0.25        Rough: 0.2            ← Apply

[Apply All Reference Values]
```

**Benefits**:
- Quick material setup
- Physically accurate starting points
- Educational reference

**Effort**: Low (data-driven, minimal code)

---

## Category 5: Performance & Optimization

### 5.1 Material Complexity Analyzer

**Purpose**: Measure and display material rendering cost

**Metrics**:
- Texture count (+ memory usage)
- Shader complexity score
- Features used (transmission, clearcoat, sheen, etc.)
- Estimated GPU cost (relative)

**Implementation**:
```javascript
function analyzeMaterialCost(material) {
    let cost = 0;
    let textures = 0;
    let memory = 0;

    // Count textures
    ['map', 'normalMap', 'roughnessMap', 'metalnessMap',
     'emissiveMap', 'aoMap', 'clearcoatMap', ...].forEach(prop => {
        if (material[prop]) {
            textures++;
            const tex = material[prop];
            memory += tex.image.width * tex.image.height * 4; // RGBA
            cost += 10; // Base texture sampling cost
        }
    });

    // Feature costs
    if (material.transmission > 0) cost += 50; // Expensive
    if (material.clearcoat > 0) cost += 20;
    if (material.sheen > 0) cost += 15;
    if (material.iridescence > 0) cost += 25;

    return {
        textures,
        memoryMB: memory / (1024 * 1024),
        relativeCost: cost,
        complexity: cost < 50 ? 'Low' : cost < 150 ? 'Medium' : 'High'
    };
}
```

**UI**:
```
Material Performance:
━━━━━━━━━━━━━━━━━━━━━━━━━━━

Complexity: Medium
Relative Cost: 127 / 500

Textures: 7
Memory: 24.5 MB

Features:
✓ Transmission (HIGH COST)
✓ Clearcoat
✓ Normal Mapping
✓ PBR Workflow

Optimization Suggestions:
• Reduce texture resolution (4K→2K saves 18MB)
• Consider removing transmission if not needed
• Combine roughness+metalness into ORM texture
```

**Benefits**:
- Optimize material performance
- Budget memory usage
- Identify expensive materials

**Effort**: Low-Medium (mostly data collection)

---

### 5.2 Texture Memory Profiler

**Purpose**: Show texture memory usage breakdown

**Features**:
- List all textures with dimensions and memory usage
- Sort by size, identify largest textures
- Suggest optimizations (compression, resolution reduction)
- Show total GPU memory used

**UI**:
```
┌─────────────────────────────────────┐
│ Texture Memory Profiler             │
├─────────────────────────────────────┤
│ Total: 156.8 MB                     │
│                                      │
│ Texture              Size      Mem  │
│ ─────────────────────────────────── │
│ base_color.png       4096²    64MB │
│ normal_map.png       4096²    64MB │
│ roughness.png        2048²    16MB │
│ metalness.png        2048²    16MB │
│ env_map.hdr          1024²     4MB │
│                                      │
│ Suggestions:                         │
│ • Reduce base_color to 2K (save 48MB)│
│ • Use BC7 compression (save ~60%)   │
│ • Combine rough+metal+ao into ORM   │
└─────────────────────────────────────┘
```

**Effort**: Low (enumerate textures, calculate sizes)

---

### 5.3 Shader Variant Counter

**Purpose**: Show how many shader variants are compiled

**Background**:
Three.js compiles different shader variants based on material features (textures, lights, etc.). Too many variants = slow loading.

**Features**:
- Count unique shader programs
- Show which features cause variants
- Suggest material batching strategies

**Implementation**:
```javascript
// Hook into WebGLRenderer's shader compilation
const originalGetProgram = renderer.getProgram;
const shaderVariants = new Set();

renderer.getProgram = function(...args) {
    const program = originalGetProgram.apply(this, args);
    const hash = getProgramHash(program);
    shaderVariants.add(hash);
    return program;
};

console.log(`Compiled ${shaderVariants.size} shader variants`);
```

**Benefits**:
- Reduce shader compilation time
- Optimize material batching
- Improve loading performance

**Effort**: Low (instrumentation + tracking)

---

## Category 6: Interactive Material Editing

### 6.1 Real-Time Material Parameter Tweaker

**Purpose**: Live-edit material parameters with instant visual feedback

**Features** (already partially implemented via lil-gui):
- Sliders for all PBR parameters
- Color pickers for base color, specular, emission
- Texture slot management (swap textures on-the-fly)
- **NEW**: Parameter presets and curves

**Enhanced Features**:

#### Parameter Linking
```javascript
// Link multiple materials to edit together
const linkedMaterials = [mat1, mat2, mat3];
function setLinkedRoughness(value) {
    linkedMaterials.forEach(mat => mat.roughness = value);
}
```

#### Parameter Animation
```javascript
// Animate parameters over time for testing
animateParameter({
    material: selectedMaterial,
    property: 'roughness',
    from: 0.0,
    to: 1.0,
    duration: 3000, // ms
    easing: 'easeInOut'
});
```

#### Randomization for Variation
```javascript
// Add random variation to create material variations
function randomizeParameter(base, variation) {
    return base + (Math.random() - 0.5) * variation;
}

// Apply to create 10 wood plank variations
for (let i = 0; i < 10; i++) {
    materials[i].roughness = randomizeParameter(0.6, 0.2);
    materials[i].color.r = randomizeParameter(0.4, 0.1);
}
```

**Effort**: Low-Medium (builds on existing GUI)

---

### 6.2 Material Gradient/Ramp Editor

**Purpose**: Create smooth parameter transitions across surface

**Use Cases**:
- Weathering effects (clean → rusty)
- Wear patterns (new → worn)
- Depth-based variation (wet at bottom, dry at top)

**Implementation**:
```glsl
// Use vertex color, world position, or custom attribute
float t = vPosition.y; // Height-based gradient

// Interpolate between two material states
vec3 baseColorTop = vec3(0.8, 0.8, 0.8);
vec3 baseColorBottom = vec3(0.3, 0.2, 0.1);
vec3 baseColor = mix(baseColorBottom, baseColorTop, t);

float roughnessTop = 0.2;
float roughnessBottom = 0.8;
float roughness = mix(roughnessBottom, roughnessTop, t);
```

**UI**:
```
Gradient Editor:
━━━━━━━━━━━━━━━━━━━━━━━━━━━

Gradient Type: [Height-based ▼]

Top Material:
  Base Color: █████ (0.8, 0.8, 0.8)
  Roughness: 0.2
  Metalness: 0.0

Bottom Material:
  Base Color: █████ (0.3, 0.2, 0.1)
  Roughness: 0.8
  Metalness: 0.0

[Apply Gradient]
```

**Effort**: Medium (requires shader generation)

---

## Category 7: Advanced Debugging Tools

### 7.1 False Color Ranges with Custom Mapping

**Purpose**: Enhanced false color with customizable ranges and gradients

**Features** (extends existing false color):
- Custom min/max range (not just 0-1)
- Multiple gradient presets:
  - Grayscale
  - Heat map (blue → red)
  - Rainbow
  - Turbo (perceptually uniform)
  - Custom (user-defined colors)
- Isolate specific value ranges (highlight roughness 0.3-0.5 in red)

**UI**:
```
False Color Settings:
━━━━━━━━━━━━━━━━━━━━━━━━━━━

Mode: [Roughness ▼]

Range: [0.0] ──────●─── [1.0]

Gradient: [Heat Map ▼]
  0.0 ████ Blue
  0.5 ████ Yellow
  1.0 ████ Red

Isolate Range:
  [ ] Highlight values [0.3] to [0.5]

[Apply]
```

**Effort**: Low (shader-based, extends existing)

---

### 7.2 Material Override System

**Purpose**: Temporarily override specific material properties for all objects

**Use Cases**:
- Set all materials to same roughness to isolate roughness effect
- Force all materials to non-metallic to check base color
- Override all normal maps to flat to see lighting without bump detail

**UI**:
```
Material Overrides:
━━━━━━━━━━━━━━━━━━━━━━━━━━━

[×] Override Roughness → 0.5
[×] Override Metalness → 0.0
[ ] Override Base Color → White
[ ] Disable Normal Maps
[ ] Disable All Textures

[Reset All Overrides]
```

**Implementation**:
```javascript
function applyMaterialOverrides(overrides) {
    scene.traverse(obj => {
        if (obj.isMesh && obj.material) {
            // Store original
            if (!originalMaterialProps.has(obj)) {
                originalMaterialProps.set(obj, {
                    roughness: obj.material.roughness,
                    metalness: obj.material.metalness,
                    // ...
                });
            }

            // Apply overrides
            if (overrides.roughness !== null) {
                obj.material.roughness = overrides.roughness;
            }
            // ...
        }
    });
}
```

**Effort**: Low-Medium (material property management)

---

### 7.3 Pixel Inspector (Magnifying Glass)

**Purpose**: Examine individual pixels in detail

**Features**:
- Magnify area around cursor (5×5 or 10×10 pixel grid)
- Show exact RGB values for each pixel
- Display all G-buffer channels for inspected pixel
- Compare neighboring pixels (detect discontinuities)

**UI**:
```
┌─────────────────────────────┐
│ Pixel Inspector (12× zoom) │
├─────────────────────────────┤
│  ┌───┬───┬───┬───┬───┐     │
│  │   │   │ ● │   │   │     │
│  ├───┼───┼───┼───┼───┤     │
│  │   │   │ █ │   │   │     │
│  ├───┼───┼───┼───┼───┤     │
│  │ ● │ █ │ █ │ █ │ ● │     │
│  └───┴───┴───┴───┴───┘     │
│                              │
│ Center Pixel (640, 480):    │
│  RGB: 178, 142, 98          │
│  Albedo: 0.72, 0.58, 0.42  │
│  Roughness: 0.65            │
│  Metalness: 0.0             │
│  Normal: 0.1, 0.8, 0.5      │
│  Depth: 12.4m               │
└─────────────────────────────┘
```

**Implementation**:
- Render to texture
- Use `readPixels()` on NxN region
- Display as magnified grid

**Effort**: Medium (requires render target, pixel readback)

---

### 7.4 Shader Error Visualization

**Purpose**: Highlight pixels where shading produces invalid results

**Detections**:
- NaN (Not a Number) values
- Infinite values
- Negative values where impossible (e.g., negative roughness)
- Out-of-range HDR values (> 10,000)

**Implementation**:
```glsl
vec3 shadedColor = computePBR(...);

// Detect errors
bool hasNaN = isnan(shadedColor.r) || isnan(shadedColor.g) || isnan(shadedColor.b);
bool hasInf = isinf(shadedColor.r) || isinf(shadedColor.g) || isinf(shadedColor.b);
bool tooHigh = any(greaterThan(shadedColor, vec3(10000.0)));

if (hasNaN) {
    gl_FragColor = vec4(1, 0, 1, 1); // Magenta
} else if (hasInf) {
    gl_FragColor = vec4(1, 1, 0, 1); // Yellow
} else if (tooHigh) {
    gl_FragColor = vec4(1, 0.5, 0, 1); // Orange
} else {
    gl_FragColor = vec4(shadedColor, 1.0);
}
```

**Benefits**:
- Catch shader bugs immediately
- Identify numerical instability
- Debug HDR/tonemapping issues

**Effort**: Low (shader instrumentation)

---

## Category 8: Educational & Documentation

### 8.1 Interactive PBR Theory Guide

**Purpose**: Built-in educational overlay explaining PBR concepts

**Features**:
- Click on material property → show explanation
- Interactive examples (adjust roughness, see specular lobe change)
- Annotated diagrams (Fresnel curve, BRDF lobes)
- Links to external resources (PBR guides, papers)

**Example**:
```
┌─────────────────────────────┐
│ What is Roughness?          │
├─────────────────────────────┤
│ Roughness controls how      │
│ smooth or rough a surface   │
│ appears.                     │
│                              │
│ 0.0 = Mirror (perfect       │
│       reflection)            │
│ 1.0 = Matte (diffuse)       │
│                              │
│ [Interactive Demo]           │
│  Roughness: [====|====] 0.5 │
│                              │
│  ●───→ Light                │
│  │     ╱╲  Specular lobe    │
│  │    ╱  ╲ (wider = rougher)│
│  └───────→                  │
│                              │
│ Learn more: [PBR Guide →]   │
└─────────────────────────────┘
```

**Effort**: Medium-High (content creation, interactive demos)

---

### 8.2 Material Property Tooltips

**Purpose**: Contextual help when hovering over parameters

**Features**:
- Hover over parameter → show typical range, units, examples
- Visual preview (show what changing this does)
- Common mistakes warnings

**Example**:
```
Roughness: [====●====] 0.35
           ↓
    ┌──────────────────────────┐
    │ Roughness                │
    │ ───────────────────────  │
    │ Range: 0.0 - 1.0         │
    │ Current: 0.35 (Glossy)   │
    │                           │
    │ Examples:                 │
    │  0.0-0.2: Mirror, chrome │
    │  0.2-0.5: Glossy plastic │
    │  0.5-0.8: Matte plastic  │
    │  0.8-1.0: Rough concrete │
    │                           │
    │ ⚠ Tip: For metals, use   │
    │   lower roughness values │
    └──────────────────────────┘
```

**Effort**: Low (tooltip system + content)

---

## Category 9: Export & Sharing

### 9.1 Debug Screenshot Exporter

**Purpose**: Export screenshots with annotations and data overlays

**Features**:
- Capture current view with G-buffer channels
- Export multi-panel comparison (Final | Albedo | Normal | Roughness)
- Embed material parameters as metadata
- Generate HTML report with images + data

**Output Example**:
```
material_debug_report.html:

┌─────────────────────────────────┐
│ Material Debug Report           │
│ Sphere.001 - GoldMaterial       │
│ Timestamp: 2025-01-21 10:30     │
├─────────────────────────────────┤
│ [Final Render] [Albedo] [Normal]│
│ [Roughness] [Metalness] [Depth] │
│                                  │
│ Material Properties:             │
│  Base Color: (1.0, 0.766, 0.336)│
│  Metalness: 1.0                  │
│  Roughness: 0.25                 │
│  IOR: 0.47                       │
│                                  │
│ Textures:                        │
│  base_color_map: 2048×2048 sRGB │
│  roughness_map: 1024×1024 Linear│
│                                  │
│ Validation:                      │
│  ✓ Energy conserving            │
│  ✓ Physically plausible IOR     │
│  ⚠ Base color very saturated    │
└─────────────────────────────────┘
```

**Effort**: Medium (screenshot capture, HTML generation)

---

### 9.2 Material Preset Save/Load

**Purpose**: Save current material state as preset for reuse

**Features**:
- Save material parameters to JSON
- Save with screenshot thumbnail
- Organize into categories (metals, plastics, organics)
- One-click apply to other objects

**File Format**:
```json
{
  "name": "Brushed Aluminum",
  "category": "Metals",
  "thumbnail": "data:image/png;base64,...",
  "parameters": {
    "baseColor": [0.912, 0.914, 0.920],
    "metalness": 1.0,
    "roughness": 0.4,
    "anisotropy": 0.8,
    "anisotropyRotation": 0.0
  },
  "textures": {
    "normalMap": "./brushed_aluminum_normal.png",
    "roughnessMap": "./brushed_aluminum_rough.png"
  }
}
```

**UI**:
```
Material Presets:
━━━━━━━━━━━━━━━━━━━━━━━━━━━

[🔍] Search presets...

Metals:
  [📷] Gold
  [📷] Silver
  [📷] Brushed Aluminum ← You are here
  [📷] Copper

Plastics:
  [📷] Glossy Black
  [📷] Matte White

[💾 Save Current] [📂 Load]
```

**Effort**: Low-Medium (JSON serialization, thumbnail generation)

---

## Summary & Prioritization

### Priority 1 (High Impact, Low-Medium Effort) - Implement First:

1. **Advanced AOV Modes** (Ambient Occlusion, Anisotropy, Sheen, Iridescence, Normal Quality Check)
   - Effort: Low-Medium | Impact: High | Benefit: Immediate debugging value

2. **Material Validation & Linting**
   - Effort: Medium | Impact: High | Benefit: Catches errors automatically

3. **Texture Channel Inspector**
   - Effort: Medium | Impact: High | Benefit: Deep texture debugging

4. **Material Override System**
   - Effort: Low-Medium | Impact: High | Benefit: Quick isolation testing

5. **False Color Ranges with Custom Mapping**
   - Effort: Low | Impact: Medium-High | Benefit: Extends existing feature

6. **Shader Error Visualization**
   - Effort: Low | Impact: High | Benefit: Catches numerical issues

### Priority 2 (High Impact, Medium-High Effort) - Implement Next:

7. **Real-Time G-Buffer Viewer**
   - Effort: Medium | Impact: High | Benefit: See all channels at once

8. **IBL Contribution Analyzer**
   - Effort: Medium | Impact: Medium-High | Benefit: Understand lighting

9. **Material Complexity Analyzer**
   - Effort: Low-Medium | Impact: Medium | Benefit: Performance optimization

10. **Side-by-Side Material Comparison**
    - Effort: Medium | Impact: Medium-High | Benefit: Validation workflows

### Priority 3 (Nice to Have):

11. UV Layout Overlay
12. Mip-Map Level Visualizer
13. Reference Material Library
14. Pixel Inspector
15. Material Preset Save/Load
16. Debug Screenshot Exporter

### Priority 4 (Advanced/Specialized):

17. BRDF Visualizer
18. Light Probe Visualizer
19. Texture Tiling Detector
20. Material Gradient/Ramp Editor
21. Interactive PBR Theory Guide

---

## Implementation Roadmap

### Phase 1: Core Debugging (2-3 weeks)
- Advanced AOV modes
- Material validation
- Shader error visualization
- Material override system

### Phase 2: Texture & Performance (2-3 weeks)
- Texture channel inspector
- G-Buffer viewer
- Material complexity analyzer
- UV debugging tools

### Phase 3: Lighting & Comparison (2-3 weeks)
- IBL contribution analyzer
- Material comparison tools
- False color enhancements

### Phase 4: Polish & Documentation (1-2 weeks)
- Material presets
- Debug export
- Tooltips & help system
- Educational content

**Total Estimated Effort**: 8-11 weeks for full implementation

---

## Technical Architecture Recommendations

### Rendering Architecture:
```javascript
class DebugRenderPipeline {
    constructor(renderer, scene, camera) {
        this.renderer = renderer;
        this.scene = scene;
        this.camera = camera;

        // Render targets for G-buffer
        this.gBuffer = {
            albedo: new THREE.WebGLRenderTarget(...),
            normal: new THREE.WebGLRenderTarget(...),
            roughness: new THREE.WebGLRenderTarget(...),
            metalness: new THREE.WebGLRenderTarget(...),
            depth: new THREE.WebGLRenderTarget(...),
            // ... more channels
        };

        // Material override system
        this.materialOverrides = new Map();

        // Validation engine
        this.validator = new MaterialValidator();
    }

    renderGBuffer() {
        // Multi-pass rendering to fill G-buffer
    }

    analyzeScene() {
        // Run validation checks
        return this.validator.validate(this.scene);
    }

    applyDebugVisualization(mode) {
        // Apply AOV or debug mode
    }
}
```

### Plugin System for Extensibility:
```javascript
class DebugPlugin {
    constructor(name, description) {
        this.name = name;
        this.description = description;
    }

    // Called when plugin is activated
    onActivate(renderPipeline) {}

    // Called each frame
    onRender(renderPipeline) {}

    // Provide UI elements
    getUIElements() { return []; }
}

// Example plugin
class NormalMapQualityChecker extends DebugPlugin {
    onActivate(pipeline) {
        pipeline.addAOVMode('normal_quality', this.createShader());
    }

    createShader() {
        return new THREE.ShaderMaterial({
            // ... normal validation shader
        });
    }
}
```

### Data-Driven Configuration:
```javascript
// debug-config.json
{
  "aov_modes": [
    {
      "id": "ambient_occlusion",
      "name": "Ambient Occlusion",
      "shader": "aov/ao.glsl",
      "enabled": true
    },
    // ... more modes
  ],
  "validation_rules": [
    {
      "id": "energy_conservation",
      "severity": "error",
      "check": "materials.baseColor * materials.metalness <= 1.0"
    },
    // ... more rules
  ],
  "reference_materials": {
    // ... reference data
  }
}
```

---

## Conclusion

These enhancements would transform the MaterialX web demo into a **comprehensive PBR debugging and material authoring tool**, competitive with professional DCC tools like Substance Designer or Marmoset Toolbag's material inspection features.

**Key Benefits**:
- Faster material debugging (identify issues in seconds vs. minutes)
- Educational value (learn PBR concepts interactively)
- Production-ready validation (catch errors before export)
- Performance optimization (identify costly materials)
- Better MaterialX/USD workflows (validate conversions)

**Next Steps**:
1. Review and prioritize features with stakeholders
2. Create detailed technical specifications for Phase 1
3. Begin implementation with highest-priority items
4. Iterate based on user feedback
