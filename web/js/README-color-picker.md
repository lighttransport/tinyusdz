# Color Picker

A real-time color picker that samples pixel values directly from the rendered framebuffer in the TinyUSDZ MaterialX web demo.

## Overview

The Color Picker allows you to click anywhere in the rendered 3D view to extract the exact color value at that pixel. It reads directly from the WebGL framebuffer and displays the color in multiple formats useful for different workflows.

## Features

- **Direct Framebuffer Sampling**: Reads actual rendered pixel values using `gl.readPixels()`
- **Multiple Color Formats**: RGB (0-255), Hex, Float (0-1), Linear RGB (0-1)
- **Visual Feedback**: Shows picked color in a swatch
- **Position Display**: Shows both mouse coordinates and WebGL pixel coordinates
- **Copy to Clipboard**: One-click copying of any format
- **Toggle Mode**: Enable/disable picker without losing other functionality
- **Non-Destructive**: Doesn't interfere with normal navigation when disabled

## Usage

### Activating Color Picker Mode

1. Click the **🎨 Color Picker** button in the top toolbar
2. The button will highlight and the cursor will change to a crosshair
3. The color picker panel will appear on the right side

### Picking Colors

1. With color picker mode active, click anywhere in the 3D viewport
2. The color at that exact pixel will be sampled
3. The color picker panel will update with:
   - **Color swatch**: Visual preview of the picked color
   - **RGB**: Integer values (0-255) for red, green, blue
   - **Hex**: Hexadecimal color code (e.g., `#A3B5C7`)
   - **Float**: Normalized values (0-1) in sRGB space
   - **Linear**: Scene-linear RGB values (0-1) for shader work
   - **Position**: Mouse coordinates and framebuffer pixel coordinates

### Copying Color Values

Click any of the "Copy" buttons next to the color formats:
- **Copy RGB**: Copies "128, 192, 255" format
- **Copy Hex**: Copies "#80C0FF" format
- **Copy Float**: Copies "0.5020, 0.7529, 1.0000" format
- **Copy Linear**: Copies "0.2140, 0.5225, 1.0000" format

The button will show "✓ Copied!" confirmation for 1.5 seconds.

### Deactivating Color Picker Mode

Click the **🎨 Color Picker** button again to:
- Disable picker mode
- Restore normal object selection
- Keep the color picker panel visible with last picked color

Close the panel entirely by clicking outside it or toggling the button twice.

## Color Format Explanations

### RGB (0-255)
Standard 8-bit integer RGB values as stored in the framebuffer.

```
RGB: 128, 192, 255
```

**Use cases**:
- CSS color values
- Image editing software
- General color communication

### Hex
Hexadecimal color code for web/CSS use.

```
Hex: #80C0FF
```

**Use cases**:
- HTML/CSS development
- Design tools (Figma, Sketch)
- Color documentation

### Float (0-1, sRGB)
Normalized RGB values in sRGB color space.

```
Float: 0.5020, 0.7529, 1.0000
```

**Use cases**:
- Three.js color constructor: `new THREE.Color(0.5020, 0.7529, 1.0000)`
- Blender color inputs
- General 3D software

**Conversion formula**:
```
float_value = rgb_value / 255.0
```

### Linear (0-1, Linear RGB)
Scene-linear RGB values used for physically accurate rendering.

```
Linear: 0.2140, 0.5225, 1.0000
```

**Use cases**:
- MaterialX color inputs
- OpenPBR Surface parameters
- Shader development
- Physically-based rendering workflows

**Conversion formula** (sRGB to Linear):
```javascript
if (srgb_value <= 0.04045) {
    linear_value = srgb_value / 12.92;
} else {
    linear_value = pow((srgb_value + 0.055) / 1.055, 2.4);
}
```

This matches the standard sRGB → Linear RGB transfer function.

## Position Display

The position shows two coordinate systems:

```
Position: (450, 320) → (900, 640)
```

- **Left (450, 320)**: Mouse position in CSS pixels (origin: top-left)
- **Right (900, 640)**: WebGL framebuffer pixel coordinates (origin: bottom-left, accounting for devicePixelRatio)

**Why two coordinates?**
- Mouse events use CSS pixels
- WebGL framebuffer uses device pixels (may be 2× or more on high-DPI displays)
- Y-axis is flipped between coordinate systems

## Technical Implementation

### WebGL Pixel Reading

The color picker uses the WebGL `readPixels` API:

```javascript
const gl = renderer.getContext();
const pixelBuffer = new Uint8Array(4);

gl.readPixels(
    x,        // X coordinate (left edge)
    y,        // Y coordinate (bottom edge)
    1,        // Width (1 pixel)
    1,        // Height (1 pixel)
    gl.RGBA,  // Format
    gl.UNSIGNED_BYTE,  // Type
    pixelBuffer        // Output buffer
);

// Extract RGBA
const r = pixelBuffer[0];  // 0-255
const g = pixelBuffer[1];  // 0-255
const b = pixelBuffer[2];  // 0-255
const a = pixelBuffer[3];  // 0-255
```

### Coordinate Transformations

The implementation handles three coordinate systems:

1. **Mouse Coordinates** (from click event)
   - Origin: Top-left corner
   - Units: CSS pixels
   - Example: `(450, 320)`

2. **Canvas Coordinates** (after devicePixelRatio scaling)
   - Origin: Top-left corner
   - Units: Device pixels
   - Example: `(900, 640)` on 2× display

3. **WebGL Coordinates** (for readPixels)
   - Origin: Bottom-left corner (flipped Y)
   - Units: Device pixels
   - Example: `(900, 160)` if canvas height is 800

```javascript
// Convert mouse to canvas coordinates
const dpr = window.devicePixelRatio || 1;
const canvasX = mouseX * dpr;
const canvasY = mouseY * dpr;

// Flip Y for WebGL (origin bottom-left)
const webglX = canvasX;
const webglY = canvasHeight - canvasY;

// Clamp to valid range
const clampedX = Math.max(0, Math.min(canvasWidth - 1, webglX));
const clampedY = Math.max(0, Math.min(canvasHeight - 1, webglY));
```

### Color Space Conversion

The picker implements the standard sRGB ↔ Linear RGB conversion:

```javascript
// sRGB to Linear (for shader/material work)
function sRGBToLinear(value) {
    if (value <= 0.04045) {
        return value / 12.92;
    } else {
        return Math.pow((value + 0.055) / 1.055, 2.4);
    }
}

// Linear to sRGB (inverse, for display)
function linearToSRGB(value) {
    if (value <= 0.0031308) {
        return value * 12.92;
    } else {
        return 1.055 * Math.pow(value, 1.0 / 2.4) - 0.055;
    }
}
```

### Integration with Click Handling

The color picker integrates with the existing click handler:

```javascript
function onMouseClick(event) {
    // Priority 1: Color picker (if active)
    if (isColorPickerActive()) {
        const handled = handleColorPickerClick(event, renderer);
        if (handled) return;  // Don't proceed to object selection
    }

    // Priority 2: Normal object/material selection
    // ... existing raycasting code ...
}
```

This ensures:
- Color picker has priority when active
- Normal selection works when picker is disabled
- No interference between modes

## Use Cases

### 1. Material Debugging

When a material doesn't render as expected:

1. Enable color picker
2. Click on the problematic surface
3. Compare Linear RGB values with material parameters
4. Check if values match expected OpenPBR inputs

**Example**: If `base_color` is `[0.8, 0.2, 0.1]` but rendered color is `[0.6170, 0.0331, 0.0100]`, this confirms the sRGB→Linear conversion is working correctly.

### 2. Color Matching Across Tools

To match colors between Blender and the web viewer:

1. Pick color from rendered surface
2. Copy Float format: `0.5020, 0.7529, 1.0000`
3. Paste into Blender material node (Principled BSDF color input)
4. Colors will match exactly

### 3. MaterialX Parameter Verification

When exporting materials to MaterialX:

1. Pick color from rendered surface
2. Copy Linear format (scene-linear values)
3. Compare with MaterialX `<input>` values in exported `.mtlx` file
4. Verify OpenPBR Surface parameters match rendered result

**Example MaterialX**:
```xml
<input name="base_color" type="color3" value="0.2140, 0.5225, 1.0000" />
```

### 4. Environment Map Sampling

To sample IBL (image-based lighting) contribution:

1. Render with environment map enabled
2. Pick color from reflective surface
3. Compare with direct base color to see IBL influence
4. Adjust `envMapIntensity` based on results

### 5. Texture Color Verification

To verify texture values are loading correctly:

1. Load model with textures
2. Pick color from textured surface
3. Compare RGB values with source texture file
4. Identify colorspace issues (if texture looks too bright/dark)

### 6. Documentation and Bug Reports

When reporting rendering issues:

1. Pick color at specific location
2. Click "Export JSON" (if implemented) or copy values
3. Include color data in bug report
4. Provides exact pixel values for debugging

## Integration with Other Features

### Material JSON Viewer

Combine color picker with JSON viewer for complete material analysis:

1. Select object with material
2. Open Material JSON viewer (📋 Material JSON button)
3. Enable color picker
4. Click surface to see rendered color
5. Compare picked Linear RGB with OpenPBR `base_color` in JSON
6. Verify material parameters match rendered output

**Example Workflow**:
```
OpenPBR JSON: "base_color": [0.8, 0.2, 0.1]
Picked Linear: 0.6170, 0.0331, 0.0100  ✓ Matches (gamma-corrected)
```

### Node Graph Viewer

Use color picker to understand node graph output:

1. Open Node Graph viewer
2. Identify material output node
3. Enable color picker
4. Click rendered surface
5. Verify output matches node graph connections

### MaterialX Export

Verify exported MaterialX accuracy:

1. Select material
2. Export as MaterialX (.mtlx)
3. Reload exported file
4. Pick same location on surface
5. Compare color values (should match exactly)

## Browser Compatibility

### WebGL readPixels Support

- ✅ **Chrome/Edge 90+**: Full support
- ✅ **Firefox 88+**: Full support
- ✅ **Safari 14+**: Full support
- ⚠️ **Older browsers**: May not support RGBA/UNSIGNED_BYTE readPixels

### Clipboard API

- ✅ **Modern browsers**: `navigator.clipboard.writeText()` supported
- ⚠️ **HTTP (non-HTTPS)**: May require user permission
- ⚠️ **Older browsers**: Copy feature may not work

### Device Pixel Ratio

- ✅ **High-DPI displays**: Correctly accounts for 2×, 3× scaling
- ✅ **Standard displays**: Works with 1× pixel ratio

## Performance Considerations

### Single Pixel Read

Reading one pixel is very fast:
- **Cost**: ~0.1ms per readPixels call
- **Impact**: Negligible for click-based picking

### GPU → CPU Readback

`readPixels` causes GPU→CPU synchronization:
- **Warning**: Don't call every frame
- **Current implementation**: Only on click (✓ Good)
- **Avoid**: Continuous readPixels in animation loop

### Coordinate Calculations

Position transformations are optimized:
- **Cost**: <0.01ms for coordinate math
- **No impact** on rendering performance

## Comparison with Other Tools

| Feature | Color Picker | Browser DevTools Color Picker | External Eyedropper |
|---------|--------------|-------------------------------|---------------------|
| Sample from WebGL | ✓ | ✗ (DOM only) | ✗ |
| Linear RGB output | ✓ | ✗ | ✗ |
| Exact pixel coordinates | ✓ | Partial | ✗ |
| Copy multiple formats | ✓ | Limited | ✗ |
| MaterialX workflow | ✓ | ✗ | ✗ |
| No extension needed | ✓ | ✓ | ✗ |

## Limitations

### Alpha Channel

Currently displays alpha value but doesn't affect color display:
- RGB values shown are pre-multiplied
- Transparency not reflected in color swatch
- Future enhancement: Show checkered background for transparent colors

### Color Precision

Limited by 8-bit framebuffer:
- Each channel: 0-255 (256 levels)
- Float precision: 4 decimal places shown
- Dithering may affect single-pixel reads

### Post-Processing

Samples the final rendered output:
- Includes tone mapping (if enabled)
- Includes gamma correction
- Includes any post-processing effects
- Linear values reverse sRGB gamma only (not tone mapping)

### Picking Accuracy

Cursor may not align perfectly with picked pixel:
- Crosshair is CSS-based (not pixel-perfect)
- High-DPI displays may have sub-pixel offsets
- Solution: Position display shows exact pixel coordinates

## Troubleshooting

### "No color picked yet"

**Cause**: Trying to copy before picking any color

**Solution**: Click somewhere in the viewport with picker mode enabled first

### Color swatch doesn't match viewport

**Cause**: Possible colorspace mismatch or post-processing

**Solution**:
- Check if tone mapping is enabled
- Verify sRGB framebuffer settings
- Compare Linear vs Float values

### Position coordinates seem wrong

**Cause**: High-DPI display with devicePixelRatio > 1

**Solution**: This is normal. The right coordinates (WebGL pixels) will be higher than left (mouse pixels) on high-DPI displays.

### Copy to clipboard doesn't work

**Cause 1**: Browser doesn't support Clipboard API

**Solution**: Use a modern browser (Chrome 90+, Firefox 88+, Safari 14+)

**Cause 2**: Page not served over HTTPS

**Solution**: Use HTTPS or localhost (Clipboard API requires secure context)

### Picked color is black (0, 0, 0)

**Cause 1**: Clicked on background/empty space

**Solution**: Click on an actual rendered object

**Cause 2**: Rendering hasn't completed

**Solution**: Wait for scene to fully load and render before picking

### Colors look incorrect

**Cause**: Clicking during camera movement

**Solution**: Wait for rendering to stabilize before picking

## Keyboard Shortcuts

Currently no keyboard shortcuts are implemented, but potential additions:

- `C` - Toggle color picker mode
- `Esc` - Disable color picker mode
- `Ctrl+C` - Copy last picked color (default format)

## Future Enhancements

Potential improvements:

- [ ] Export picked colors to palette file
- [ ] Color history (save last N picked colors)
- [ ] Color comparison (pick two colors and show difference)
- [ ] Average color over NxN pixel area (reduce noise)
- [ ] Show alpha channel with checkered background
- [ ] RGB/HSV/HSL color space display
- [ ] Live preview (show color under cursor before clicking)
- [ ] Color gradient analysis (sample line between two points)
- [ ] HDR color support (for >1.0 values with tone mapping)
- [ ] Export as .ase (Adobe Swatch Exchange) palette

## Related Documentation

- **[Material JSON Viewer](./README-json-viewer.md)** - Inspect material parameters
- **[Node Graph Viewer](./README-node-graph.md)** - Visualize shader networks
- **[OpenPBR Parameters Reference](../../../doc/openpbr-parameters-reference.md)** - Parameter details
- **[MaterialX Support](../../../doc/materialx.md)** - MaterialX color workflows

## Example Workflows

### Workflow 1: Verify Base Color

Goal: Confirm material base color matches rendered output

1. Select object with material
2. Open Material JSON viewer → OpenPBR tab
3. Note `base_color` value: `[0.8, 0.2, 0.1]`
4. Enable color picker
5. Click on the surface
6. Compare Linear RGB: `0.6170, 0.0331, 0.0100`
7. Verify gamma-corrected match: `pow(0.8, 2.2) ≈ 0.617` ✓

### Workflow 2: Debug Texture Colors

Goal: Verify texture is loading with correct color values

1. Load model with base color texture
2. Enable color picker
3. Click on textured surface
4. Note RGB values: `204, 102, 51`
5. Open source texture in image editor
6. Sample same location: `204, 102, 51` ✓ Match
7. If mismatch, check texture colorspace settings

### Workflow 3: Match Colors to Reference

Goal: Extract exact color from reference model

1. Load reference USD file
2. Enable color picker
3. Click on surface to match
4. Copy Float values: `0.7529, 0.3765, 0.1882`
5. In Blender, paste into Principled BSDF Base Color
6. Export as USD with MaterialX
7. Colors will match exactly in both renderers

### Workflow 4: Analyze Environment Lighting

Goal: Understand IBL contribution to material

1. Render scene with environment map
2. Pick color from metallic surface: `0.4500, 0.5200, 0.6000` (Linear)
3. Set `envMapIntensity` to 0
4. Re-render and pick again: `0.1000, 0.1000, 0.1000` (Linear)
5. Calculate IBL contribution: `(0.45-0.10, 0.52-0.10, 0.60-0.10) = (0.35, 0.42, 0.50)`
6. Adjust `envMapIntensity` based on desired lighting strength

## License

Part of the TinyUSDZ project (Apache 2.0 License).
