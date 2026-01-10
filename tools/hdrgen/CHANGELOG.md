# HDRGen Changelog

## Version 1.1.0 (2025-11-06)

### ✨ New Features

#### Image Transformations
- **Rotation Support** - Rotate environment maps around Y axis
  - Use `--rotation <degrees>` (positive = counterclockwise)
  - Bilinear filtering for smooth results
  - Useful for adjusting lighting direction

- **Intensity Scaling** - Global intensity multiplier
  - Use `--intensity-scale <factor>` or `--scale <factor>`
  - Quickly adjust overall brightness
  - Apply after preset generation

#### LDR Output Formats
- **PNG Output** - 8-bit RGB PNG format
  - Automatic tone mapping from HDR
  - Simplified implementation (no compression)
  - Use `--format png` or `-f png`

- **BMP Output** - 24-bit RGB BMP format
  - Uncompressed bitmap format
  - Widely compatible
  - Use `--format bmp` or `-f bmp`

- **JPEG Placeholder** - JPEG support noted
  - Currently converts to BMP
  - Requires jpeg-js library for true JPEG encoding
  - Use `--format jpg` or `--format jpeg`

#### Tone Mapping
- **Multiple Tone Mapping Methods**
  - `simple` - Exposure + clamp
  - `reinhard` - Reinhard operator (default)
  - `aces` - ACES filmic tone curve
  - Use `--tonemap-method <method>`

- **Exposure Control**
  - `--exposure <ev>` - Exposure adjustment in EV stops
  - Default: 0.0
  - Positive values brighten, negative darken

- **Gamma Correction**
  - `--gamma <value>` - Gamma correction for display
  - Default: 2.2 (standard for sRGB displays)
  - Adjustable for different display profiles

### 🔧 API Changes

**HDRGenerator.generate() new options:**
```javascript
{
  rotation: 0,              // Rotation in degrees
  intensityScale: 1.0,      // Intensity multiplier
  format: 'hdr',            // Now supports: hdr, exr, png, bmp, jpg
  tonemapOptions: {         // For LDR output
    exposure: 0.0,
    gamma: 2.2,
    method: 'reinhard'
  }
}
```

**New Classes Exported:**
- `ImageTransform` - Image rotation and scaling utilities
- `ToneMapper` - HDR to LDR tone mapping
- `LDRWriter` - LDR format writers (PNG, BMP, JPEG)

### 📝 Examples

**Generate LDR preview:**
```bash
hdrgen -p sun-sky -f png --exposure 1.0 -o output/sky_preview.png
```

**Rotate environment 90 degrees:**
```bash
hdrgen -p studio --rotation 90 -o output/studio_rotated.hdr
```

**Scale intensity 2x and output as BMP:**
```bash
hdrgen -p studio --intensity-scale 2.0 -f bmp -o output/studio_bright.bmp
```

**ACES tone mapping:**
```bash
hdrgen -p sun-sky -f png --tonemap-method aces --exposure -0.5 -o output/sky_aces.png
```

### 🐛 Bug Fixes

- Fixed CRC32 calculation in PNG writer (was producing negative values)
- Added unsigned 32-bit coercion for correct CRC generation

### 📊 Code Statistics

- **New Code:** ~400 lines
- **Total Code:** ~1,500 lines
- **New Classes:** 3 (ImageTransform, ToneMapper, LDRWriter)
- **New Formats:** 3 (PNG, BMP, JPEG placeholder)

---

## Version 1.0.0 (2025-11-06)

### Initial Release

- HDR/EXR environment map generation
- Three presets: white-furnace, sun-sky, studio
- Lat-long and cubemap projections
- Pure Node.js implementation (zero dependencies)
- Comprehensive documentation

### Features

- **Presets:**
  - White Furnace - Energy conservation testing
  - Sun & Sky - Procedural outdoor environment
  - Studio Lighting - 3-point lighting setup

- **Formats:**
  - HDR (Radiance RGBE) - Fully implemented
  - EXR (OpenEXR) - Stub implementation

- **Projections:**
  - Lat-long (equirectangular)
  - Cubemap (6 faces)

- **CLI:**
  - Comprehensive command-line interface
  - Preset-specific options
  - Resolution control

- **Documentation:**
  - Complete README (15KB)
  - Quick start guide
  - API documentation
  - DCC integration guides

### Code Statistics

- **Total Code:** ~1,100 lines
- **Test Coverage:** 8 unit tests
- **Examples:** 7+ preset variations
