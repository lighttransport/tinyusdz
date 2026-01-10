## PBR Debugging Tools - User Guide

Comprehensive guide to the PBR material debugging tools in the TinyUSDZ MaterialX web demo.

## Overview

The MaterialX demo provides professional-grade debugging tools for inspecting and validating PBR (Physically-Based Rendering) materials, comparable to tools like Substance Designer and Marmoset Toolbag.

**Available Tools**:
1. **Advanced AOV Modes** - Visualize material properties separately
2. **Material Validator** - Automatic error detection and linting
3. **Material Override System** - Global property overrides for testing
4. **Split View Comparison** - Side-by-side material comparison
5. **Color Picker** - Sample final rendered colors
6. **Material Property Picker** - Sample material values before lighting

---

## 1. Advanced AOV Modes

AOV (Arbitrary Output Variable) modes let you visualize individual material properties by replacing the final shading with diagnostic views.

### Available AOV Modes

#### Ambient Occlusion (AO)
**Purpose**: Visualize ambient occlusion maps

**What it shows**:
- White = fully exposed areas (AO = 1.0)
- Black = occluded areas (AO = 0.0)
- Samples red channel of AO texture

**Use cases**:
- Verify AO map loaded correctly
- Check AO intensity
- Identify baking artifacts

**How to use**:
```javascript
// Enable AO visualization
applyAOVMode('ambient_occlusion');
```

---

#### Anisotropy
**Purpose**: Debug anisotropic materials (brushed metal, hair)

**What it shows**:
- **Hue** = anisotropy direction (rotation)
- **Brightness** = anisotropy strength
- Samples anisotropy map (RG=direction, B=strength)

**Use cases**:
- Verify brushed metal direction
- Check anisotropy strength
- Debug hair/fabric anisotropy

**Example values**:
- Brushed aluminum: strength=0.8, rotation varies by brush direction
- Hair: strength=0.6-0.9, rotation follows hair flow

---

#### Sheen
**Purpose**: Visualize fabric sheen layer

**What it shows**:
- RGB = sheen color * sheen strength
- Alpha = sheen roughness

**Use cases**:
- Debug velvet, cloth, fabric materials
- Verify sheen color and intensity
- Check sheen roughness variation

**Example materials**:
- Velvet: sheen=1.0, color=(0.1, 0.05, 0.05), roughness=0.3
- Satin: sheen=0.8, color=(1, 1, 1), roughness=0.1

---

#### Iridescence
**Purpose**: Visualize thin-film interference effects

**What it shows**:
- **R channel** = iridescence strength (0-1)
- **G channel** = normalized thickness
- **B channel** = IOR normalized to [0,1] range

**Use cases**:
- Debug soap bubbles, oil slicks
- Verify iridescence thickness variation
- Check IOR values

**Example values**:
- Soap bubble: strength=1.0, thickness=100-400nm, IOR=1.3
- Oil slick: strength=0.8, thickness=200-600nm, IOR=1.4

---

#### Normal Quality Check
**Purpose**: Validate normal map data quality

**Color coding**:
- 🟢 **Green** = Valid normals (vector length ≈ 1.0)
- 🟡 **Yellow** = Warning (length deviation 0.05-0.1)
- 🔴 **Red** = Error (length deviation > 0.1)

**Use cases**:
- Detect corrupted normal maps
- Find incorrectly baked normals
- Verify normal map format (OpenGL vs DirectX)

**Common issues detected**:
- Unnormalized vectors (length ≠ 1.0)
- Compression artifacts
- Wrong tangent space basis

**How to fix**:
- Red areas: Re-export or re-bake normal map
- Yellow areas: May be acceptable, check if visible
- Green areas: Normal map is valid

---

#### UV Layout Overlay
**Purpose**: Visualize UV coordinates and detect layout issues

**What it shows**:
- **Base color**: R=U coordinate, G=V coordinate
- **White grid lines**: UV layout at adjustable frequency
- **Red highlights**: UV seams (detected using derivatives)

**Use cases**:
- Verify UV unwrapping
- Identify UV stretching (distorted grid)
- Find UV seams causing visible artifacts
- Check texture tiling alignment

**Adjustable parameters**:
```javascript
// In createAOVMaterial for UV_LAYOUT:
gridFrequency: 8.0,  // Number of grid squares
lineWidth: 0.05,     // Grid line thickness
```

**Interpreting the view**:
- **Square grid** = uniform UV layout (good)
- **Stretched grid** = UV distortion (may cause texture stretching)
- **Red lines** = UV seams (can cause visible splits in textures)

---

#### Shader Error Detection
**Purpose**: Catch numerical errors in shader calculations

**Color coding**:
- 🟢 **Green** = Valid values
- 🟣 **Magenta** = NaN (Not a Number)
- 🟡 **Yellow** = Infinity
- 🟠 **Orange** = Values too high (>10,000)
- 🔵 **Cyan** = Negative values (where shouldn't be)

**Use cases**:
- Detect division by zero
- Find NaN propagation
- Identify HDR overflow
- Debug shader bugs

**What causes errors**:
- NaN: 0/0, sqrt(-1), log(-1)
- Inf: 1/0, exp(large), pow(large, large)
- Too high: Unbounded HDR calculations
- Negative: Improper color/roughness calculations

---

### Using AOV Modes

**Enable AOV mode**:
```javascript
// Apply AOV mode to scene
applyAOVMode('ambient_occlusion');
applyAOVMode('normal_quality');
applyAOVMode('uv_layout');
```

**Disable AOV (restore normal rendering)**:
```javascript
applyAOVMode('none');
// or
restoreOriginalMaterials();
```

**Switch between modes**:
```javascript
setAOVMode('albedo');      // Built-in
setAOVMode('roughness');   // Built-in
setAOVMode('anisotropy');  // New
setAOVMode('sheen');       // New
```

---

## 2. Material Validation & Linting

Automatically detect common material errors and PBR best practices violations.

### Validation Rules

#### Energy Conservation (Warning)
**Checks**: `baseColor * metalness ≤ 1.0`

**Why**: Metallic materials can't reflect more light than they receive

**Example error**:
```
Base color too bright for metallic material (1.2 > 1.0)
```

**How to fix**: Reduce base color brightness or metalness value

---

#### IOR Range (Warning)
**Checks**: `1.0 ≤ IOR ≤ 3.0`

**Why**: Physical materials have IOR in this range

**Common values**:
- Air: 1.0
- Water: 1.33
- Glass: 1.5-1.9
- Diamond: 2.42

**Example error**:
```
IOR 0.8 < 1.0 (physically impossible)
IOR 4.5 unusually high (typical range: 1.0-3.0)
```

---

#### Metallic IOR (Info)
**Checks**: Metallic materials shouldn't use dielectric IOR (1.5)

**Why**: Metals have complex IOR values

**Example warning**:
```
Metallic material using dielectric IOR (1.5).
Metals have complex IOR.
```

**Note**: Three.js/MaterialX doesn't use complex IOR, so this is informational

---

#### Texture Power-of-Two (Info)
**Checks**: Texture dimensions are powers of 2 (256, 512, 1024, 2048, 4096)

**Why**: Optimal for GPU mipmapping and compression

**Example warning**:
```
Non-power-of-2 textures may cause issues:
map: 1000×1000 (not power-of-2)
normalMap: 2000×2000 (not power-of-2)
```

**How to fix**: Resize textures to nearest power-of-2

---

#### Colorspace Validation (Error)
**Checks**:
- Base color maps use sRGB
- Normal/roughness/metalness maps use Linear

**Why**: Incorrect colorspace causes wrong colors/lighting

**Example errors**:
```
❌ Base color map should use sRGB encoding
❌ Normal map incorrectly using sRGB encoding (should be Linear)
❌ Data textures incorrectly using sRGB:
   roughnessMap using sRGB (should be Linear)
```

**How to fix**:
```javascript
// Set correct encoding
baseColorTexture.encoding = THREE.sRGBEncoding;
normalMapTexture.encoding = THREE.LinearEncoding;
roughnessMapTexture.encoding = THREE.LinearEncoding;
```

---

#### Other Validation Rules

**Missing Normal Map** (Info):
- Suggests adding normal map if PBR maps present but no normals

**Zero Roughness** (Info):
- Warns about perfect mirrors (roughness < 0.01)
- Real materials have some roughness

**Intermediate Metalness** (Info):
- Warns about metalness values between 0.1-0.9
- Should usually be 0 (dielectric) or 1 (metal)
- Exception: Painted metal, oxidized metal

**Bright Base Color** (Warning):
- Base color > 0.95 for dielectrics is unusual
- Most dielectrics have albedo < 0.9

**Dark Base Color for Metals** (Info):
- Metals with average albedo < 0.5 are rare
- Most metals are bright (silver, aluminum, gold)

---

### Using Material Validator

**Validate single material**:
```javascript
const validator = new MaterialValidator();
const result = validator.validate(material);

console.log(`Errors: ${result.errors.length}`);
console.log(`Warnings: ${result.warnings.length}`);
console.log(`Passed: ${result.passedCount}`);
```

**Validate entire scene**:
```javascript
const sceneResults = validator.validateScene(scene);
validator.logResults(sceneResults);
```

**Console output example**:
```
🔍 Material Validation Results
Materials: 12/12
❌ Errors: 2
⚠️ Warnings: 5
ℹ️ Info: 8

Material: GoldMaterial
  ❌ Base Color Colorspace: Base color map should use sRGB encoding
  ⚠️ Energy Conservation: Base color too bright for metallic material (1.15 > 1.0)

Material: PlasticMaterial
  ℹ️ Missing Normal Map: Consider adding one for more detail
```

**Generate report**:
```javascript
const report = validator.generateReport(sceneResults);
console.log(report); // Markdown format
// Save to file or display in UI
```

---

## 3. Material Override System

Temporarily override material properties globally for debugging.

### Quick Presets

#### Base Color Only
```javascript
applyOverridePreset(scene, 'BASE_COLOR_ONLY');
```
- Disables all textures
- Sets roughness=0.5, metalness=0.0
- Shows only material base colors

**Use case**: Verify base color values without texture influence

---

#### Normals Only
```javascript
applyOverridePreset(scene, 'NORMALS_ONLY');
```
- Gray base color
- Disables all textures except normals
- Roughness=0.5, metalness=0.0

**Use case**: See only normal mapping effect

---

#### Flat Shading
```javascript
applyOverridePreset(scene, 'FLAT_SHADING');
```
- Disables normal maps only
- Keeps all other properties

**Use case**: Compare with vs. without bump detail

---

#### Mirror
```javascript
applyOverridePreset(scene, 'MIRROR');
```
- Roughness=0.0 (perfect reflection)
- Metalness=1.0

**Use case**: Test environment map reflections

---

#### Matte
```javascript
applyOverridePreset(scene, 'MATTE');
```
- Roughness=1.0 (fully diffuse)
- Metalness=0.0

**Use case**: Test pure diffuse lighting

---

#### White Clay
```javascript
applyOverridePreset(scene, 'WHITE_CLAY');
```
- Base color=(0.8, 0.8, 0.8)
- Roughness=0.6, metalness=0.0
- Disables all textures

**Use case**: Material preview style (like ZBrush/Blender matcaps)

---

### Custom Overrides

**Override specific property**:
```javascript
// Override roughness globally
applyMaterialOverrides(scene, { roughness: 0.3 });

// Override metalness
applyMaterialOverrides(scene, { metalness: 1.0 });

// Override base color
applyMaterialOverrides(scene, {
    baseColor: new THREE.Color(1, 0, 0) // Red
});
```

**Disable specific textures**:
```javascript
// Disable only normal maps
applyMaterialOverrides(scene, { disableNormalMaps: true });

// Disable specific map types
applyMaterialOverrides(scene, {
    disableMaps: {
        base: true,      // Disable base color maps
        normal: false,   // Keep normal maps
        roughness: true, // Disable roughness maps
        metalness: true  // Disable metalness maps
    }
});
```

**Reset overrides**:
```javascript
resetMaterialOverrides(scene);
```

---

### Override Workflows

**Workflow 1: Isolate Roughness Effect**
```javascript
// 1. Override all materials to same base values
applyMaterialOverrides(scene, {
    baseColor: new THREE.Color(0.5, 0.5, 0.5),
    metalness: 0.0,
    disableAllTextures: true
});

// 2. Adjust roughness and observe specular lobe changes
applyMaterialOverrides(scene, { roughness: 0.0 });  // Mirror
applyMaterialOverrides(scene, { roughness: 0.5 });  // Semi-glossy
applyMaterialOverrides(scene, { roughness: 1.0 });  // Matte

// 3. Reset
resetMaterialOverrides(scene);
```

**Workflow 2: Debug Texture Issues**
```javascript
// Compare textured vs. constant values
applyOverridePreset(scene, 'BASE_COLOR_ONLY'); // No textures
// vs.
resetMaterialOverrides(scene); // With textures

// If they look very different, textures may have issues
```

**Workflow 3: Verify Normal Maps**
```javascript
// Toggle normals on/off
applyMaterialOverrides(scene, { disableNormalMaps: true });
// vs.
applyMaterialOverrides(scene, { disableNormalMaps: false });

// If no difference, normal maps may not be loaded
```

---

## 4. Split View Comparison

Side-by-side comparison with horizontal, vertical, and diagonal split modes.

### Split Modes

#### Vertical Split (Left/Right)
```javascript
const splitView = new SplitViewComparison(renderer, scene, camera);
splitView.setSplitMode('vertical');
splitView.enable();

// In render loop:
splitView.render();
```

**Use case**: Compare two materials side-by-side

---

#### Horizontal Split (Top/Bottom)
```javascript
splitView.setSplitMode('horizontal');
```

**Use case**: Compare before/after edits

---

#### Diagonal Split
```javascript
splitView.setSplitMode('diagonal');
```

**Use case**: Artistic comparison, wipe transitions

---

### Comparison Presets

#### Final vs Albedo
```javascript
// Show final render on left, albedo on right
splitView.enable();
splitView.applyAOVToSecondary(createAOVMaterial('albedo'));
```

**Use case**: See how much lighting affects final appearance

---

#### With vs Without Normals
```javascript
splitView.enable();
splitView.applyMaterialToSecondary(material => {
    material.normalMap = null;
    material.needsUpdate = true;
});
```

**Use case**: Isolate normal mapping effect

---

#### Metallic vs Dielectric
```javascript
splitView.enable();
splitView.applyMaterialToSecondary(material => {
    material.metalness = material.metalness > 0.5 ? 0.0 : 1.0;
    material.needsUpdate = true;
});
```

**Use case**: Compare metallic vs dielectric appearance

---

### Interactive Split Position

**Drag to adjust divider**:
```javascript
canvas.addEventListener('mousemove', (event) => {
    if (isDraggingSplitter) {
        splitView.handleMouseMove(event, canvas);
    }
});
```

**Set split position programmatically**:
```javascript
splitView.setSplitPosition(0.3);  // 30% left, 70% right
splitView.setSplitPosition(0.5);  // 50/50
splitView.setSplitPosition(0.7);  // 70% left, 30% right
```

---

## 5. Combined Workflows

### Workflow 1: Complete Material Debugging

```javascript
// 1. Validate material
const validator = new MaterialValidator();
const results = validator.validate(material);
validator.logResults({ materials: [results] });

// 2. Check for errors
if (results.errors.length > 0) {
    console.error('Fix these errors first:', results.errors);
}

// 3. Visualize individual properties
applyAOVMode('normal_quality');  // Check normals
applyAOVMode('uv_layout');       // Check UVs
applyAOVMode('shader_error');    // Check for NaN/Inf

// 4. Test with overrides
applyOverridePreset(scene, 'MIRROR');  // Test reflections
applyOverridePreset(scene, 'MATTE');   // Test diffuse

// 5. Compare states
splitView.enable();
splitView.setSplitMode('vertical');
// Left: current, Right: modified

// 6. Reset
resetMaterialOverrides(scene);
applyAOVMode('none');
splitView.disable();
```

---

### Workflow 2: Texture Debugging

```javascript
// 1. Check normal map quality
applyAOVMode('normal_quality');
// Look for red areas = errors

// 2. Check UV layout
applyAOVMode('uv_layout');
// Look for distorted grid or red seams

// 3. Isolate texture contribution
splitView.enable();
splitView.setSplitMode('vertical');

// Left: with textures
resetMaterialOverrides(scene);

// Right: without textures
splitView.applyMaterialToSecondary(mat => {
    mat.map = null;
    mat.normalMap = null;
    mat.roughnessMap = null;
    mat.metalnessMap = null;
    mat.needsUpdate = true;
});

// 4. Validate colorspace
const validator = new MaterialValidator();
const results = validator.validateScene(scene);
// Check for colorspace errors
```

---

### Workflow 3: Metalness/Roughness Debugging

```javascript
// 1. Visualize roughness
applyAOVMode('roughness');
// Should show grayscale gradient

// 2. Visualize metalness
applyAOVMode('metalness');
// Should be mostly 0 (black) or 1 (white)

// 3. Override to test effect
applyMaterialOverrides(scene, {
    roughness: 0.0,  // Mirror
    metalness: 1.0   // Metal
});

// 4. Compare rough vs smooth
splitView.enable();
splitView.setSplitMode('horizontal');

// Top: smooth (roughness=0.0)
applyMaterialOverrides(scene, { roughness: 0.0 });

// Bottom: rough (roughness=1.0)
splitView.applyMaterialToSecondary(mat => {
    mat.roughness = 1.0;
    mat.needsUpdate = true;
});
```

---

## Tips & Best Practices

### Debugging Checklist

When a material doesn't look right:

- [ ] **Validate**: Run `MaterialValidator` to check for errors
- [ ] **Check normals**: Use `normal_quality` AOV (look for red)
- [ ] **Check UVs**: Use `uv_layout` AOV (look for distortion/seams)
- [ ] **Check textures**: Compare with/without using overrides
- [ ] **Check colorspace**: Validate base color (sRGB) vs data textures (Linear)
- [ ] **Check values**: Use Material Property Picker to sample exact values
- [ ] **Check lighting**: Use Split View to compare with/without IBL

---

### Performance Tips

- **AOV modes** are cheap (just shader replacement)
- **Material validation** is one-time (run on load)
- **Split view** costs 2× rendering (reduce complexity if slow)
- **Overrides** are instant (just property changes)

---

### Keyboard Shortcuts (If Implemented)

Suggested shortcuts:
- `1-7`: Switch AOV modes
- `V`: Toggle material validation panel
- `O`: Toggle material overrides panel
- `S`: Toggle split view
- `R`: Reset all debugging modes

---

## API Reference

See individual module files for detailed API:
- `material-validator.js` - MaterialValidator class
- `material-override.js` - Override functions and presets
- `split-view-comparison.js` - SplitViewComparison class

---

## Troubleshooting

**Q: AOV mode shows all black/white**
A: Material may not have that property. Check with Material Property Picker.

**Q: Validation shows false errors**
A: Some rules are informational. Check severity (error vs warning vs info).

**Q: Split view shows same image on both sides**
A: Ensure secondary scene is different (apply modifiers/AOV).

**Q: Overrides don't work**
A: Check if material properties exist. Some materials may not support all properties.

**Q: Normal Quality shows all red**
A: Normal map may be corrupted, wrong format, or not normalized. Re-export.

---

## License

Part of the TinyUSDZ project (Apache 2.0 License).
