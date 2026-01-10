# MaterialX Verification System

Automated verification system for MaterialX shading implementation using headless Chrome rendering and pixel-level comparison.

## Overview

This verification system provides:

1. **Headless Chrome Rendering** - Uses Puppeteer with SwiftShader fallback for GPU-less environments
2. **Visual Regression Testing** - Pixel-level comparison between TinyUSDZ and reference implementations
3. **Colorspace Validation** - Pure Node.js tests for colorspace conversions (no GPU required)
4. **Automated Reporting** - HTML reports with side-by-side comparisons and diff visualization

## System Requirements

### Required
- Node.js 18+ with ES modules support
- npm packages (automatically installed via `package.json`)

### Optional
- Google Chrome at `/opt/google/chrome/chrome` (for hardware GPU rendering)
  - If not available, bundled Chromium will be used
  - SwiftShader software rendering is used by default (no GPU required)

## Installation

```bash
cd web/js
npm install
```

This installs all required dependencies:
- `puppeteer` - Headless Chrome automation
- `pixelmatch` - Image comparison algorithm
- `pngjs` - PNG image reading/writing
- `commander` - CLI argument parsing

## Usage

### 1. Colorspace Tests (Pure Node.js)

Test colorspace conversions without requiring GPU or browser:

```bash
npm run test:colorspace
```

**Tests include:**
- sRGB ↔ Linear conversions
- Rec.709 → XYZ color space transforms
- Validation against MaterialX specification reference values

**Example output:**
```
🎨 MaterialX Colorspace Conversion Tests

✓ sRGB to Linear - Mid Gray - PASSED
  Input:    [0.500000, 0.500000, 0.500000]
  Expected: [0.214041, 0.214041, 0.214041]
  Result:   [0.214041, 0.214041, 0.214041]

✓ Passed: 9
✗ Failed: 0
Total: 9
```

### 2. Material Rendering Verification

Render materials with headless Chrome and compare against reference:

```bash
npm run verify-materialx render
```

**Options:**
```bash
# Test specific materials
npm run verify-materialx render --materials brass,glass,gold

# Use hardware GPU acceleration (if available)
npm run verify-materialx render --gpu

# Verbose output (show browser console logs)
npm run verify-materialx render --verbose

# Custom Chrome path
CHROME_PATH=/usr/bin/google-chrome npm run verify-materialx render
```

**Output:**
- Screenshots: `verification-results/screenshots/`
  - `tinyusdz-{material}.png` - Rendered with TinyUSDZ
  - `reference-{material}.png` - Rendered with MaterialX reference
- Diff images: `verification-results/diffs/`
  - `diff-{material}.png` - Pixel difference visualization
- Report: `verification-results/report.html` - Interactive HTML report

### 3. Clean Results

Remove all verification results:

```bash
npm run verify-materialx clean
```

## How It Works

### Rendering Pipeline

```
┌─────────────────────────────────────────────────────────┐
│  1. Launch Headless Chrome                              │
│     - SwiftShader (software) or GPU acceleration        │
│     - WebGL/WebGPU enabled                              │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│  2. Load Test HTML Pages                                │
│     - render-tinyusdz.html (TinyUSDZ + Three.js)        │
│     - render-reference.html (MaterialXLoader)           │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│  3. Render Material (120 frames)                        │
│     - Setup: Camera, lights, geometry, environment      │
│     - Render: Fixed 800x600 @ 1.0 pixel ratio          │
│     - Signal: window.renderComplete = true              │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│  4. Take Screenshots                                    │
│     - PNG format for lossless comparison                │
│     - Consistent viewport and rendering settings        │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│  5. Compare Images (pixelmatch)                         │
│     - Pixel-by-pixel color difference                   │
│     - Threshold: 0.1 (0-1 scale)                        │
│     - Generate diff visualization                       │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│  6. Generate Report                                     │
│     - Pass/Fail: < 2% difference = PASS                 │
│     - HTML with side-by-side comparison                 │
│     - Diff highlighting in third column                 │
└─────────────────────────────────────────────────────────┘
```

### Comparison Criteria

**Pass Conditions:**
- Average pixel difference < 2.0%
- Images have identical dimensions
- Both renderers completed successfully

**Metrics Reported:**
- Pixels different: Absolute count of non-matching pixels
- Total pixels: Width × Height
- Percent different: (pixels different / total pixels) × 100
- Status: PASSED or FAILED

## Test Materials

Default test materials (OpenPBR):

| Material | Properties |
|----------|-----------|
| **brass** | Metallic: 1.0, Roughness: 0.3, Color: Gold-brown |
| **glass** | Transmission: 1.0, IOR: 1.52, Clear |
| **gold** | Metallic: 1.0, Roughness: 0.2, Color: Gold |
| **copper** | Metallic: 1.0, Roughness: 0.25, Color: Copper |
| **plastic** | Metallic: 0.0, Roughness: 0.5, Color: Red |
| **marble** | Subsurface: 0.3, Roughness: 0.1, Color: Off-white |

## Architecture

### Files Structure

```
web/js/
├── verify-materialx.js          # Main CLI tool
├── tests/
│   ├── colorspace-test.js       # Pure Node.js colorspace tests
│   ├── render-tinyusdz.html     # TinyUSDZ renderer page
│   └── render-reference.html    # MaterialX reference renderer page
├── verification-results/        # Generated output (gitignored)
│   ├── screenshots/
│   ├── diffs/
│   └── report.html
└── VERIFICATION_README.md       # This file
```

### Technologies

- **Puppeteer**: Chrome automation and screenshot capture
- **SwiftShader**: Software GPU implementation (no hardware GPU required)
- **pixelmatch**: Perceptual image comparison algorithm
- **Three.js**: WebGL rendering framework
- **MaterialXLoader**: Official Three.js MaterialX loader

## SwiftShader Rendering

SwiftShader is used by default for reproducible, GPU-independent rendering:

**Advantages:**
- ✓ No GPU hardware required
- ✓ Consistent results across machines
- ✓ Works in CI/CD environments
- ✓ Deterministic rendering

**Launch arguments:**
```javascript
--disable-gpu
--use-gl=swiftshader
--use-angle=swiftshader
```

**Performance:**
- Rendering: ~2-5 seconds per material
- Screenshot: Instant
- Comparison: < 100ms

## CI/CD Integration

Example GitHub Actions workflow:

```yaml
name: MaterialX Verification

on: [push, pull_request]

jobs:
  verify:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Setup Node.js
        uses: actions/setup-node@v3
        with:
          node-version: '18'

      - name: Install dependencies
        run: |
          cd web/js
          npm install

      - name: Run colorspace tests
        run: npm run test:colorspace

      - name: Run rendering verification
        run: npm run verify-materialx render

      - name: Upload report
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: verification-report
          path: web/js/verification-results/
```

## Troubleshooting

### Chrome not found

**Error:** "Chrome not found at /opt/google/chrome/chrome"
**Solution:** Puppeteer will use bundled Chromium automatically

Or set custom path:
```bash
export CHROME_PATH=/usr/bin/google-chrome
npm run verify-materialx render
```

### Rendering timeout

**Error:** "Timeout waiting for renderComplete"
**Causes:**
- WebGL/WebGPU not available
- JavaScript errors in test page
- Network issues loading Three.js modules

**Solution:** Check browser console with `--verbose`:
```bash
npm run verify-materialx render --verbose
```

### Image dimension mismatch

**Error:** "Image dimensions do not match"
**Cause:** Different viewport sizes between renderers
**Solution:** Both test HTML pages use fixed 800x600 viewport

### High pixel difference

**Error:** "FAILED (≥ 2% difference)"
**Causes:**
- MaterialX parameter mismatch
- Shader implementation differences
- Lighting or environment differences
- Colorspace handling differences

**Solution:** Review diff image in `verification-results/diffs/`

## Extending Tests

### Add New Material

Edit test HTML files to add new material definitions:

```javascript
const MATERIALS = {
  // ... existing materials ...

  myMaterial: {
    name: 'MyMaterial',
    base_color: [0.5, 0.7, 0.9],
    base_metalness: 0.5,
    base_roughness: 0.4,
    // ... other OpenPBR parameters
  }
};
```

Then run:
```bash
npm run verify-materialx render --materials myMaterial
```

### Add New Colorspace Test

Edit `tests/colorspace-test.js`:

```javascript
const tests = [
  // ... existing tests ...

  {
    name: 'My Custom Test',
    input: [0.5, 0.5, 0.5],
    operation: 'srgb_to_linear',
    expected: [0.214041, 0.214041, 0.214041],
    tolerance: 0.001
  }
];
```

## Future Enhancements

- [ ] Test with real ASWF MaterialX example files
- [ ] Add texture-mapped material tests
- [ ] Performance benchmarking
- [ ] WebGPU rendering path comparison
- [ ] Automated regression tracking
- [ ] Statistical analysis of rendering differences
- [ ] Support for custom geometry (not just sphere)
- [ ] Export MaterialX XML from TinyUSDZ for roundtrip tests

## References

- [MaterialX Official Site](https://materialx.org/)
- [ASWF MaterialX Repository](https://github.com/AcademySoftwareFoundation/MaterialX)
- [MaterialX Web Viewer](https://academysoftwarefoundation.github.io/MaterialX/)
- [Three.js MaterialXLoader](https://threejs.org/docs/#examples/en/loaders/MaterialXLoader)
- [SwiftShader](https://github.com/google/swiftshader)
