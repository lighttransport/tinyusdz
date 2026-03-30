# UI Integration Summary - PBR Debugging Tools

## Overview

This document summarizes the UI integration of the Priority 1 PBR debugging features into the MaterialX web demo.

**Date**: 2025-01-21
**Status**: ✅ Complete

---

## Changes Made

### 1. materialx.js Updates

**Import Additions** (lines 42-49):
```javascript
import {
    applyMaterialOverrides,
    resetMaterialOverrides,
    applyOverridePreset,
    OVERRIDE_PRESETS
} from './material-override.js';
import { MaterialValidator } from './material-validator.js';
import { SplitViewComparison, COMPARISON_PRESETS } from './split-view-comparison.js';
```

**AOV Dropdown Enhancements** (lines 3010-3044):
Added 7 new AOV visualization modes to the dropdown:
- `UV Layout Overlay` - Grid lines + seam detection
- `Ambient Occlusion` - AO map visualization
- `Anisotropy` - Brushed metal direction/strength
- `Sheen` - Fabric material sheen
- `Iridescence` - Thin-film effects
- `Normal Quality Check` - Validates normal maps (green=valid, red=error)
- `Shader Error Detection` - Detects NaN/Inf/range errors

**Material Override Controls** (lines 3048-3116):
```javascript
const overrideFolder = gui.addFolder('Material Override');
```
- Enable/Disable toggle
- Roughness override slider (0-1)
- Metalness override slider (0-1)
- Disable Normal Maps checkbox
- Disable All Textures checkbox
- Preset dropdown:
  - Base Color Only
  - Normals Only
  - Flat Shading
  - Mirror
  - Matte
  - White Clay
- Reset All Overrides button

**Material Validation Controls** (lines 3118-3148):
```javascript
const validationFolder = gui.addFolder('Material Validation');
```
- Validate Now button (runs 12 validation rules)
- Error count display (read-only)
- Warning count display (read-only)
- Info count display (read-only)
- Auto-validate on Load checkbox

**Split View Comparison Controls** (lines 3150-3236):
```javascript
const splitViewFolder = gui.addFolder('Split View Compare');
```
- Enable Split View toggle
- Split Mode dropdown:
  - Vertical (Left/Right)
  - Horizontal (Top/Bottom)
  - Diagonal
- Split Position slider (0-1)
- Secondary View dropdown:
  - Material (Original)
  - Albedo
  - Normals (World)
  - Roughness
  - Metalness
  - UV Layout

**Animation Loop Update** (lines 5453-5467):
```javascript
// Check if split view comparison is enabled
if (window.splitViewComparison && window.splitViewComparison.getState().active) {
    window.splitViewComparison.render();
} else {
    // Normal rendering...
}
```

**Auto-Validation** (lines 3500-3510):
Auto-validates materials when loading if "Auto-validate on Load" is enabled.

**Window Exports** (lines 5484-5491):
```javascript
window.applyMaterialOverrides = applyMaterialOverrides;
window.resetMaterialOverrides = resetMaterialOverrides;
window.applyOverridePreset = applyOverridePreset;
window.MaterialValidator = MaterialValidator;
window.SplitViewComparison = SplitViewComparison;
window.COMPARISON_PRESETS = COMPARISON_PRESETS;
window.OVERRIDE_PRESETS = OVERRIDE_PRESETS;
```

---

### 2. materialx.html Updates

**Script Tag Additions** (lines 1208-1211):
```html
<!-- PBR Debugging Tools (must load before main script) -->
<script type="module" src="material-override.js"></script>
<script type="module" src="material-validator.js"></script>
<script type="module" src="split-view-comparison.js"></script>
```

---

## Feature Access Guide

### How to Use Each Feature

#### 1. Advanced AOV Modes
1. Load a USD file with PBR materials
2. Open the **"AOV Visualization"** folder in the right panel
3. Select from the "AOV Mode" dropdown:
   - **UV Layout Overlay**: Shows UV grid + seams (red highlights)
   - **Ambient Occlusion**: Visualizes AO maps in grayscale
   - **Anisotropy**: Direction as hue, strength as brightness
   - **Sheen**: Shows fabric sheen color + roughness
   - **Iridescence**: R=strength, G=thickness, B=IOR
   - **Normal Quality Check**: Green=valid, Yellow=warning, Red=error
   - **Shader Error Detection**: Magenta=NaN, Yellow=Inf, Orange=high values

#### 2. Material Override System
1. Open the **"Material Override"** folder in the right panel
2. Enable "Enable Overrides" checkbox
3. Adjust sliders or toggle checkboxes:
   - **Roughness Override**: Force all materials to specific roughness
   - **Metalness Override**: Force all materials to specific metalness
   - **Disable Normal Maps**: Remove normal mapping globally
   - **Disable All Textures**: Show only material constant values
4. Or select a preset from **"Apply Preset"** dropdown:
   - **Base Color Only**: Textures off, neutral roughness/metalness
   - **Normals Only**: Gray base color, only normals visible
   - **Flat Shading**: Disable normal maps
   - **Mirror**: Roughness=0, Metalness=1
   - **Matte**: Roughness=1, Metalness=0
   - **White Clay**: Material preview style (gray, no textures)
5. Click **"Reset All Overrides"** to restore original materials

#### 3. Material Validation
1. Open the **"Material Validation"** folder in the right panel
2. Click **"🔍 Validate Scene"** to run validation
3. View results:
   - **Errors** (red): Critical issues (wrong colorspace, physically impossible values)
   - **Warnings** (yellow): Best practice violations (energy conservation, unusual IOR)
   - **Info** (blue): Suggestions (missing normal maps, intermediate metalness)
4. Check browser console for detailed report
5. Enable **"Auto-validate on Load"** to run validation automatically when loading files

**Validation Rules** (12 total):
- Energy conservation (baseColor * metalness ≤ 1.0)
- IOR range (1.0-3.0)
- Metallic IOR (complex IOR for metals)
- Texture power-of-two dimensions
- Base color colorspace (sRGB required)
- Normal map colorspace (Linear required)
- Data texture colorspace (roughness/metalness/AO must be Linear)
- Missing normal map suggestions
- Zero roughness warnings (perfect mirrors rare in reality)
- Intermediate metalness warnings (should be 0 or 1)
- Bright base color warnings (dielectrics usually <0.9)
- Dark base color for metals (metals are usually bright)

#### 4. Split View Comparison
1. Open the **"Split View Compare"** folder in the right panel
2. Enable **"Enable Split View"** checkbox
3. Configure split:
   - **Split Mode**: Choose Vertical, Horizontal, or Diagonal
   - **Split Position**: Adjust divider position (0.0-1.0)
   - **Secondary View**: Choose what to show on the right/bottom:
     - Material (Original): Normal rendering
     - Albedo: Base color only
     - Normals (World): World-space normals
     - Roughness: Grayscale roughness
     - Metalness: Grayscale metalness
     - UV Layout: UV grid overlay
4. Drag the split position slider to compare different regions
5. Disable to return to normal single-view rendering

**Use Cases**:
- Compare final render vs. albedo
- Compare with vs. without normal maps
- Compare metallic vs. dielectric materials
- Compare different roughness values
- Inspect UV layout overlaid on render

---

## Files Modified

1. **web/js/materialx.js**
   - Added 3 imports for new modules
   - Extended AOV dropdown with 7 new modes
   - Added Material Override folder with controls
   - Added Material Validation folder with controls
   - Added Split View Compare folder with controls
   - Updated animate() to support split view rendering
   - Added auto-validation on material load
   - Exposed new classes to window object

2. **web/js/materialx.html**
   - Added 3 script tags to import new modules

---

## Testing Checklist

To verify the integration works correctly:

- [ ] Load a USD file with PBR materials
- [ ] Open AOV dropdown and test new modes (UV Layout, AO, Anisotropy, etc.)
- [ ] Open Material Override folder
  - [ ] Enable overrides and adjust roughness slider
  - [ ] Enable overrides and adjust metalness slider
  - [ ] Toggle "Disable Normal Maps"
  - [ ] Toggle "Disable All Textures"
  - [ ] Try each preset (Mirror, Matte, White Clay, etc.)
  - [ ] Click "Reset All Overrides"
- [ ] Open Material Validation folder
  - [ ] Click "Validate Scene" button
  - [ ] Check console for validation report
  - [ ] Verify error/warning/info counts update
  - [ ] Enable "Auto-validate on Load" and reload a file
- [ ] Open Split View Compare folder
  - [ ] Enable split view
  - [ ] Try Vertical split mode
  - [ ] Try Horizontal split mode
  - [ ] Try Diagonal split mode
  - [ ] Adjust split position slider
  - [ ] Change Secondary View to Albedo
  - [ ] Change Secondary View to UV Layout
  - [ ] Disable split view

---

## Known Limitations

1. **Split View Diagonal Mode**: Stencil buffer drawing not fully implemented (placeholder)
2. **Material Override**: Doesn't override emission color (only roughness/metalness/base color)
3. **Validation**: Runs on current scene state (not original USD data)
4. **Auto-Validation**: Only triggers on material load, not on manual edits

---

## Architecture Notes

### Module Dependencies
```
materialx.js
  ├─ material-override.js (exports functions + OVERRIDE_PRESETS)
  ├─ material-validator.js (exports MaterialValidator class)
  └─ split-view-comparison.js (exports SplitViewComparison class + COMPARISON_PRESETS)
```

### Global Scope Exports
All debugging tools are exported to `window` object for easy access:
- `window.MaterialValidator`
- `window.SplitViewComparison`
- `window.applyMaterialOverrides(scene, overrides)`
- `window.resetMaterialOverrides(scene)`
- `window.applyOverridePreset(scene, presetName)`

### GUI Integration Pattern
All features use **lil-gui** folders:
1. Create params object with initial values
2. Add controllers (sliders, dropdowns, buttons)
3. Attach onChange handlers to update scene
4. Call `gui.controllersRecursive().forEach(c => c.updateDisplay())` to refresh

---

## Performance Notes

- **AOV Modes**: Replace all materials with custom shaders (may be slow for large scenes)
- **Material Override**: Stores original properties in Map (low overhead)
- **Validation**: Iterates all meshes/materials (O(n) complexity)
- **Split View**: Renders scene twice per frame (2x render cost)

For large scenes (>1000 meshes):
- Keep AOV modes closed when not in use
- Disable split view when not comparing
- Run validation manually instead of auto-validate

---

## Future Enhancements

Remaining Priority 1 features not yet implemented:
1. **Texture Channel Inspector** (pending)
   - Histogram visualization
   - Per-channel statistics
   - Issue detection (all zeros, clamped values)

UI improvements:
- Draggable split view divider with mouse
- Validation results panel (instead of console-only)
- Material override color picker for base color
- Split view quick presets (Final vs Albedo, With vs Without Normals)

---

## Commit Summary

**Added**:
- Material Override UI controls (folder with 7 controls)
- Material Validation UI controls (folder with 5 displays)
- Split View Comparison UI controls (folder with 4 controls)
- 7 new AOV modes to dropdown (UV Layout, AO, Anisotropy, Sheen, Iridescence, Normal Quality, Shader Error)
- Auto-validation on material load
- Split view rendering in animation loop
- Module imports and window exports

**Modified**:
- `web/js/materialx.js` (+250 lines)
- `web/js/materialx.html` (+4 lines)

**Total Lines Added**: ~254 lines of UI integration code

---

## Related Documentation

- [PBR-DEBUGGING-STATUS.md](./PBR-DEBUGGING-STATUS.md) - Implementation status tracking
- [README-pbr-debugging-tools.md](./README-pbr-debugging-tools.md) - User guide for all debugging tools
- [material-override.js](./material-override.js) - Material override implementation
- [material-validator.js](./material-validator.js) - Validation system implementation
- [split-view-comparison.js](./split-view-comparison.js) - Split view implementation

---

**Last Updated**: 2025-01-21
**Status**: ✅ Ready for testing and user feedback
