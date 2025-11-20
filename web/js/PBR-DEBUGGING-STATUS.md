# PBR Debugging Features - Implementation Status

Status of Priority 1 PBR debugging features implementation for the MaterialX web demo.

## ✅ Completed Features

### 1. Advanced AOV Modes (DONE)
**Commit**: 19fa32ca

Implemented 7 new AOV visualization modes:

| Mode | Purpose | Implementation | Status |
|------|---------|----------------|--------|
| **Ambient Occlusion** | Visualize AO maps | Samples red channel, shows intensity | ✅ |
| **Anisotropy** | Debug brushed metal/hair | Direction as hue, strength as brightness | ✅ |
| **Sheen** | Debug fabric materials | Shows sheen color and roughness | ✅ |
| **Iridescence** | Thin-film effects | R=strength, G=thickness, B=IOR | ✅ |
| **Normal Quality Check** | Validate normal maps | Red=error, Yellow=warning, Green=valid | ✅ |
| **UV Layout Overlay** | UV debugging | Grid lines + seam detection | ✅ |
| **Shader Error Detection** | Catch numerical errors | NaN, Inf, range checking | ✅ |

**Code Location**: `web/js/materialx.js` lines 978-1396

**Usage**: Select from AOV dropdown menu (needs UI update to expose new modes)

---

## 🚧 In Progress / Remaining Priority 1 Features

### 2. Material Validation & Linting (TODO)
**Priority**: High | **Effort**: Medium

**What to Implement**:
- Energy conservation checks (baseColor * metalness ≤ 1.0)
- IOR range validation (1.0-3.0)
- Texture compatibility checks (power-of-2, colorspace)
- Color space validation (sRGB for base color, Linear for data textures)
- Missing texture warnings

**Implementation Plan**:
```javascript
// File: web/js/material-validator.js
class MaterialValidator {
    validate(material) {
        const warnings = [];
        const errors = [];

        // Check energy conservation
        if (material.color.r * material.metalness > 1.0) {
            warnings.push({
                type: 'energy_conservation',
                message: 'Base color too bright for metallic material',
                severity: 'warning'
            });
        }

        // Check IOR range
        if (material.ior < 1.0 || material.ior > 3.0) {
            warnings.push({
                type: 'ior_range',
                message: `IOR ${material.ior} outside typical range [1.0-3.0]`,
                severity: 'warning'
            });
        }

        // Check texture dimensions
        ['map', 'normalMap', 'roughnessMap', 'metalnessMap'].forEach(texName => {
            if (material[texName]) {
                const tex = material[texName];
                if (!isPowerOfTwo(tex.image.width) || !isPowerOfTwo(tex.image.height)) {
                    warnings.push({
                        type: 'texture_size',
                        message: `${texName} not power-of-2: ${tex.image.width}×${tex.image.height}`,
                        severity: 'info'
                    });
                }
            }
        });

        // Check colorspace encoding
        if (material.map && material.map.encoding !== THREE.sRGBEncoding) {
            errors.push({
                type: 'colorspace',
                message: 'Base color map should use sRGB encoding',
                severity: 'error'
            });
        }

        if (material.normalMap && material.normalMap.encoding === THREE.sRGBEncoding) {
            errors.push({
                type: 'colorspace',
                message: 'Normal map incorrectly using sRGB encoding',
                severity: 'error'
            });
        }

        return { warnings, errors };
    }
}
```

**UI**: Panel showing validation results with errors/warnings count

---

### 3. Texture Channel Inspector (TODO)
**Priority**: High | **Effort**: Medium-High

**What to Implement**:
- Click on material → show texture details
- Per-channel histogram (R, G, B, A distribution)
- Statistics: min, max, average, std deviation
- Issue detection:
  - All zeros (not loaded)
  - Clamped values (0 or 255 only)
  - Unexpected range
  - Single color (no variation)

**Implementation Plan**:
```javascript
// File: web/js/texture-inspector.js
class TextureInspector {
    analyzeTexture(texture) {
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');
        const img = texture.image;

        canvas.width = img.width;
        canvas.height = img.height;
        ctx.drawImage(img, 0, 0);

        const imageData = ctx.getImageData(0, 0, img.width, img.height);
        const data = imageData.data;

        const stats = {
            r: { min: 255, max: 0, sum: 0, histogram: new Array(256).fill(0) },
            g: { min: 255, max: 0, sum: 0, histogram: new Array(256).fill(0) },
            b: { min: 255, max: 0, sum: 0, histogram: new Array(256).fill(0) },
            a: { min: 255, max: 0, sum: 0, histogram: new Array(256).fill(0) }
        };

        // Analyze pixels
        for (let i = 0; i < data.length; i += 4) {
            const r = data[i];
            const g = data[i + 1];
            const b = data[i + 2];
            const a = data[i + 3];

            // Update stats
            ['r', 'g', 'b', 'a'].forEach((ch, idx) => {
                const val = data[i + idx];
                stats[ch].min = Math.min(stats[ch].min, val);
                stats[ch].max = Math.max(stats[ch].max, val);
                stats[ch].sum += val;
                stats[ch].histogram[val]++;
            });
        }

        // Calculate averages
        const pixelCount = data.length / 4;
        stats.r.avg = stats.r.sum / pixelCount;
        stats.g.avg = stats.g.sum / pixelCount;
        stats.b.avg = stats.b.sum / pixelCount;
        stats.a.avg = stats.a.sum / pixelCount;

        // Detect issues
        const issues = [];
        if (stats.r.max === 0 && stats.g.max === 0 && stats.b.max === 0) {
            issues.push('All zeros - texture may not be loaded');
        }
        if (stats.r.min === stats.r.max) {
            issues.push('R channel is constant');
        }
        // ... more checks

        return { stats, issues };
    }

    renderHistogram(canvas, histogram) {
        const ctx = canvas.getContext('2d');
        const width = canvas.width;
        const height = canvas.height;

        const maxCount = Math.max(...histogram);
        const barWidth = width / 256;

        ctx.clearRect(0, 0, width, height);
        ctx.fillStyle = '#4CAF50';

        for (let i = 0; i < 256; i++) {
            const barHeight = (histogram[i] / maxCount) * height;
            ctx.fillRect(i * barWidth, height - barHeight, barWidth, barHeight);
        }
    }
}
```

**UI**: Modal panel with texture preview, histograms, and statistics

---

### 4. Material Override System (TODO)
**Priority**: High | **Effort**: Low-Medium

**What to Implement**:
- Global overrides for all materials:
  - Force roughness value
  - Force metalness value
  - Force base color
  - Disable normal maps
  - Disable all textures

**Implementation Plan**:
```javascript
// File: web/js/material-override.js
const originalMaterialProps = new Map();

function applyMaterialOverrides(overrides) {
    scene.traverse(obj => {
        if (obj.isMesh && obj.material) {
            // Store original if not already stored
            if (!originalMaterialProps.has(obj.uuid)) {
                originalMaterialProps.set(obj.uuid, {
                    roughness: obj.material.roughness,
                    metalness: obj.material.metalness,
                    color: obj.material.color.clone(),
                    map: obj.material.map,
                    normalMap: obj.material.normalMap,
                    roughnessMap: obj.material.roughnessMap,
                    metalnessMap: obj.material.metalnessMap
                });
            }

            // Apply overrides
            if (overrides.roughness !== null) {
                obj.material.roughness = overrides.roughness;
            }
            if (overrides.metalness !== null) {
                obj.material.metalness = overrides.metalness;
            }
            if (overrides.baseColor !== null) {
                obj.material.color.copy(overrides.baseColor);
            }
            if (overrides.disableNormalMaps) {
                obj.material.normalMap = null;
            }
            if (overrides.disableAllTextures) {
                obj.material.map = null;
                obj.material.normalMap = null;
                obj.material.roughnessMap = null;
                obj.material.metalnessMap = null;
            }

            obj.material.needsUpdate = true;
        }
    });
}

function resetMaterialOverrides() {
    scene.traverse(obj => {
        if (obj.isMesh && originalMaterialProps.has(obj.uuid)) {
            const orig = originalMaterialProps.get(obj.uuid);
            obj.material.roughness = orig.roughness;
            obj.material.metalness = orig.metalness;
            obj.material.color.copy(orig.color);
            obj.material.map = orig.map;
            obj.material.normalMap = orig.normalMap;
            obj.material.roughnessMap = orig.roughnessMap;
            obj.material.metalnessMap = orig.metalnessMap;
            obj.material.needsUpdate = true;
        }
    });
    originalMaterialProps.clear();
}
```

**UI**: Checkbox panel with sliders for override values

---

### 5. Side-by-Side Comparison (TODO)
**Priority**: High (per user request) | **Effort**: Medium-High

**What to Implement**:
- Split-screen rendering with draggable divider
- Three split modes:
  - **Horizontal** (top/bottom)
  - **Vertical** (left/right)
  - **Diagonal** (top-left/bottom-right)
- Load two different:
  - USD files (compare materials)
  - Material states (before/after edits)
  - AOV modes (albedo vs final render)

**Implementation Plan**:
```javascript
// File: web/js/split-view-comparison.js
class SplitViewComparison {
    constructor(renderer, scene1, scene2, camera) {
        this.renderer = renderer;
        this.scene1 = scene1;
        this.scene2 = scene2;
        this.camera = camera;
        this.splitMode = 'vertical'; // 'vertical', 'horizontal', 'diagonal'
        this.splitPosition = 0.5; // 0.0-1.0
    }

    render() {
        const width = this.renderer.domElement.width;
        const height = this.renderer.domElement.height;

        if (this.splitMode === 'vertical') {
            // Left side: scene1
            this.renderer.setViewport(0, 0, width * this.splitPosition, height);
            this.renderer.setScissor(0, 0, width * this.splitPosition, height);
            this.renderer.setScissorTest(true);
            this.renderer.render(this.scene1, this.camera);

            // Right side: scene2
            this.renderer.setViewport(width * this.splitPosition, 0, width * (1 - this.splitPosition), height);
            this.renderer.setScissor(width * this.splitPosition, 0, width * (1 - this.splitPosition), height);
            this.renderer.render(this.scene2, this.camera);

        } else if (this.splitMode === 'horizontal') {
            // Top: scene1
            this.renderer.setViewport(0, height * (1 - this.splitPosition), width, height * this.splitPosition);
            this.renderer.setScissor(0, height * (1 - this.splitPosition), width, height * this.splitPosition);
            this.renderer.setScissorTest(true);
            this.renderer.render(this.scene1, this.camera);

            // Bottom: scene2
            this.renderer.setViewport(0, 0, width, height * (1 - this.splitPosition));
            this.renderer.setScissor(0, 0, width, height * (1 - this.splitPosition));
            this.renderer.render(this.scene2, this.camera);

        } else if (this.splitMode === 'diagonal') {
            // Use stencil buffer for diagonal split
            // ... more complex implementation
        }

        this.renderer.setScissorTest(false);
    }

    setSplitPosition(position) {
        this.splitPosition = Math.max(0, Math.min(1, position));
    }

    setSplitMode(mode) {
        this.splitMode = mode; // 'vertical', 'horizontal', 'diagonal'
    }
}
```

**UI**:
- Toggle button to enable split view
- Draggable divider to adjust split position
- Dropdown to select split mode
- Two file/scene selectors for comparison sources

---

## 📋 Additional Priority 1 Features (From Proposal)

### 6. False Color Enhancements (TODO)
**Effort**: Low

Extend existing false color mode with:
- Custom min/max ranges
- Multiple gradient presets (grayscale, heatmap, rainbow, turbo)
- Isolate specific value ranges

### 7. Enhanced Material Property Tweaker (TODO)
**Effort**: Low-Medium

Add to existing GUI:
- Parameter linking (edit multiple materials together)
- Parameter animation (animate roughness 0→1)
- Randomization for variation

---

## 🎯 Implementation Priority Order

Based on user request and effort/impact:

1. ✅ **Advanced AOV Modes** - DONE
2. **Material Override System** - Low effort, high value for debugging
3. **Side-by-Side Comparison** - User requested, medium effort
4. **Material Validation** - Medium effort, high educational value
5. **Texture Channel Inspector** - Higher effort but very useful

---

## 📊 Overall Progress

**Completed**: 3 / 8 Priority 1 features (37.5%)
- ✅ Advanced AOV Modes (7 new modes)
- ✅ UV Layout Overlay (included in AOV)
- ✅ Shader Error Visualization (included in AOV)

**In Progress**: 0

**Remaining**: 5
- Material Validation & Linting
- Texture Channel Inspector
- Material Override System
- Side-by-Side Comparison
- False Color Enhancements

**Estimated Total Effort**: 3-4 weeks for all Priority 1 features

---

## 🚀 Quick Start Guide

### Using New AOV Modes

1. Load a USD file with PBR materials
2. Open AOV dropdown (if exposed in UI)
3. Select new modes:
   - `ambient_occlusion` - See AO maps
   - `anisotropy` - Debug brushed metal
   - `sheen` - Check fabric materials
   - `iridescence` - View thin-film effects
   - `normal_quality` - Validate normal maps (red=error, green=valid)
   - `uv_layout` - See UV grid and seams
   - `shader_error` - Detect NaN/Inf (magenta/yellow/orange)

### Color Coding

**Normal Quality Check**:
- 🟢 Green = Valid normals
- 🟡 Yellow = Warning (slight deviation)
- 🔴 Red = Error (invalid normal vectors)

**Shader Error Detection**:
- 🟢 Green = Valid values
- 🟣 Magenta = NaN (Not a Number)
- 🟡 Yellow = Infinity
- 🟠 Orange = Values too high (>10,000)
- 🔵 Cyan = Negative (where invalid)

**UV Layout**:
- Red/Green channels = UV coordinates
- White grid lines = UV layout
- 🔴 Red highlights = UV seams

---

## 📝 Notes for Developers

### Adding New AOV Modes

1. Add enum to `AOV_MODES` object
2. Add case to `createAOVMaterial()` switch statement
3. Write custom shader (vertex + fragment)
4. Extract material properties from Three.js material
5. Test with various material types

### Performance Considerations

- AOV modes are applied to all meshes in scene
- Each mode creates new ShaderMaterial instances
- Original materials are stored in `aovOriginalMaterials` Map
- Restore originals when switching back to `NONE`

### Testing Checklist

For each new feature:
- [ ] Works with textured materials
- [ ] Works with constant-value materials
- [ ] Handles missing textures gracefully
- [ ] Proper colorspace handling
- [ ] No console errors
- [ ] Performance acceptable (<100ms switch time)

---

## 🔗 Related Documentation

- [Proposal Document](./PROPOSAL-pbr-debugging-enhancements.md) - Full 21-feature proposal
- [Material Property Picker](./README-material-property-picker.md) - Sample before lighting
- [Color Picker](./README-color-picker.md) - Sample after lighting
- [Material JSON Viewer](./README-json-viewer.md) - Inspect material data

---

## 📞 Contact & Feedback

For questions, issues, or feature requests, please update the proposal document or create implementation tickets.

**Last Updated**: 2025-01-21 (Commit: 19fa32ca)
