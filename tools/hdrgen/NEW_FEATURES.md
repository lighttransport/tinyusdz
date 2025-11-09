# HDRGen v1.1.0 - New Features Guide

## Overview

HDRGen v1.1.0 adds powerful image transformation and LDR export capabilities, making it easier to generate preview images and adjust environment maps for different use cases.

## 🎨 New Features

### 1. Image Rotation

Rotate environment maps around the Y axis (vertical) to adjust lighting direction.

**Use Cases:**
- Align sun position with scene requirements
- Rotate studio lights to desired angle
- Create lighting variations without regenerating

**Command Line:**
```bash
# Rotate 90 degrees counterclockwise
hdrgen -p sun-sky --rotation 90 -o output/sky_rotated.hdr

# Rotate 180 degrees (flip horizontally)
hdrgen -p studio --rotation 180 -o output/studio_flipped.hdr

# Fine rotation (45 degrees)
hdrgen -p sun-sky --sun-azimuth 0 --rotation 45 -o output/sky_45.hdr
```

**API Usage:**
```javascript
import { HDRGenerator } from './src/hdrgen.js';

HDRGenerator.generate({
  preset: 'sun-sky',
  rotation: 90,  // Degrees, positive = CCW
  output: 'output/rotated.hdr'
});
```

**Technical Details:**
- Uses bilinear filtering for smooth results
- Wraps horizontally (seamless rotation)
- No quality loss for moderate rotations
- Applied after preset generation

---

### 2. Intensity Scaling

Global intensity multiplier for the entire environment map.

**Use Cases:**
- Quickly adjust overall brightness
- Create dimmer/brighter variations
- Normalize different presets to same intensity
- Test material response to different lighting levels

**Command Line:**
```bash
# Double intensity
hdrgen -p studio --intensity-scale 2.0 -o output/studio_bright.hdr

# Half intensity
hdrgen -p sun-sky --scale 0.5 -o output/sky_dim.hdr

# 10x intensity (for testing high dynamic range)
hdrgen -p white-furnace --intensity-scale 10.0 -o output/furnace_10x.hdr
```

**API Usage:**
```javascript
HDRGenerator.generate({
  preset: 'studio',
  intensityScale: 2.0,  // Multiply all values by 2.0
  output: 'output/bright.hdr'
});
```

**Technical Details:**
- Applied after rotation
- Multiplies all RGB values uniformly
- Maintains color ratios
- No clamping (HDR preserved)

---

### 3. LDR Output Formats

Export environment maps as standard 8-bit image formats for previews and web use.

#### PNG Output

8-bit RGB PNG with automatic tone mapping.

**Command Line:**
```bash
# Basic PNG export
hdrgen -p sun-sky -f png -o output/sky.png

# PNG with custom exposure
hdrgen -p studio -f png --exposure 1.0 -o output/studio_bright.png

# PNG with ACES tone mapping
hdrgen -p sun-sky -f png --tonemap-method aces -o output/sky_aces.png
```

**Features:**
- Automatic HDR to LDR conversion
- Simplified format (no compression)
- Cross-platform compatibility
- ~385KB for 512x256 image

**Note:** For production use, consider using `sharp` or `pngjs` libraries for compressed PNG.

#### BMP Output

24-bit RGB BMP format.

**Command Line:**
```bash
# Basic BMP export
hdrgen -p studio -f bmp -o output/studio.bmp

# BMP with custom gamma
hdrgen -p sun-sky -f bmp --gamma 1.8 -o output/sky.bmp
```

**Features:**
- Uncompressed format
- Universal compatibility
- Fast write speed
- ~385KB for 512x256 image

#### JPEG Output (Placeholder)

**Command Line:**
```bash
# Will convert to BMP
hdrgen -p sun-sky -f jpg -o output/sky.jpg
# Actual output: output/sky.bmp
```

**Note:** Currently converts to BMP. For true JPEG encoding, integrate `jpeg-js` library.

---

### 4. Tone Mapping

Convert HDR to LDR with proper tone mapping operators.

#### Tone Mapping Methods

**1. Simple (Exposure + Clamp)**
```bash
hdrgen -p sun-sky -f png --tonemap-method simple --exposure 1.0 -o output/sky_simple.png
```
- Linear exposure adjustment
- Hard clipping at 1.0
- Fast, no compression
- Good for low dynamic range scenes

**2. Reinhard (Default)**
```bash
hdrgen -p sun-sky -f png --tonemap-method reinhard -o output/sky.png
```
- Reinhard global operator: `x / (1 + x)`
- Compresses high values smoothly
- Preserves local contrast
- Best for most scenes

**3. ACES Filmic**
```bash
hdrgen -p sun-sky -f png --tonemap-method aces --exposure -0.5 -o output/sky_aces.png
```
- ACES filmic tone curve approximation
- Film-like response
- Rich shadows, smooth highlights
- Best for cinematic look

#### Exposure Control

Adjust brightness before tone mapping.

```bash
# Increase exposure by 1 EV (2x brighter)
hdrgen -p studio -f png --exposure 1.0 -o output/studio_bright.png

# Decrease exposure by 1 EV (2x darker)
hdrgen -p sun-sky -f png --exposure -1.0 -o output/sky_dark.png

# Fine adjustment (+0.5 EV)
hdrgen -p studio -f png --exposure 0.5 -o output/studio_mid.png
```

**EV Scale:**
- `+1.0` = 2x brighter
- `-1.0` = 2x darker
- `+2.0` = 4x brighter
- `-2.0` = 4x darker

#### Gamma Correction

Adjust gamma for different display profiles.

```bash
# Standard sRGB gamma (default)
hdrgen -p sun-sky -f png --gamma 2.2 -o output/sky.png

# Mac gamma
hdrgen -p sun-sky -f png --gamma 1.8 -o output/sky_mac.png

# Linear (no gamma, for further processing)
hdrgen -p sun-sky -f png --gamma 1.0 -o output/sky_linear.png
```

---

## 🎯 Common Workflows

### Generate Web Preview

Quick PNG preview for web display:

```bash
hdrgen -p sun-sky -w 1024 --height 512 -f png --exposure 0.5 --gamma 2.2 -o preview.png
```

### Rotate and Scale for Scene Matching

Adjust environment to match scene lighting:

```bash
hdrgen -p sun-sky --sun-azimuth 90 --rotation 45 --intensity-scale 1.5 -o scene_env.hdr
```

### Generate LDR Reference

Create LDR reference for comparing with renderer output:

```bash
hdrgen -p studio -f png --tonemap-method aces --exposure 0.0 -o reference.png
```

### Test Multiple Intensities

Generate intensity variations for testing:

```bash
for scale in 0.5 1.0 2.0 4.0; do
  hdrgen -p studio --intensity-scale $scale -o output/studio_${scale}x.hdr
done
```

### Generate Preview Grid

Create preview images with different tone mapping:

```bash
for method in simple reinhard aces; do
  hdrgen -p sun-sky -f png --tonemap-method $method -o preview_$method.png
done
```

---

## 📊 Performance Notes

### Rotation Performance

| Resolution | Time (approx) |
|-----------|---------------|
| 512x256 | < 1 second |
| 1024x512 | ~2 seconds |
| 2048x1024 | ~8 seconds |
| 4096x2048 | ~30 seconds |

**Optimization:** Use lower resolution for previews, rotate at target resolution for final output.

### Tone Mapping Performance

| Operation | Time | Notes |
|-----------|------|-------|
| Simple | Fast | Linear, no iterations |
| Reinhard | Fast | Single pass |
| ACES | Fast | Slightly more math |

**Note:** Tone mapping adds < 100ms for typical resolutions.

### File Sizes

| Format | 512x256 | 1024x512 | 2048x1024 |
|--------|---------|----------|-----------|
| HDR | 513 KB | 2 MB | 8 MB |
| PNG (uncompressed) | 385 KB | 1.5 MB | 6 MB |
| BMP | 385 KB | 1.5 MB | 6 MB |

---

## 🔧 API Reference

### ImageTransform Class

```javascript
import { ImageTransform, HDRImage } from './src/hdrgen.js';

// Rotate image
const rotated = ImageTransform.rotate(image, 90);

// Scale intensity (in-place)
ImageTransform.scaleIntensity(image, 2.0);
```

### ToneMapper Class

```javascript
import { ToneMapper } from './src/hdrgen.js';

// Tone map HDR to LDR
const ldrData = ToneMapper.tonemapToLDR(hdrImage, {
  exposure: 1.0,
  gamma: 2.2,
  method: 'reinhard'  // 'simple', 'reinhard', 'aces'
});
```

### LDRWriter Class

```javascript
import { LDRWriter } from './src/hdrgen.js';

// Write PNG
LDRWriter.writePNG(ldrData, width, height, 'output.png');

// Write BMP
LDRWriter.writeBMP(ldrData, width, height, 'output.bmp');
```

---

## 🎓 Tips & Best Practices

### Rotation

1. **Combine with Sun Azimuth:**
   ```bash
   # Set sun to north, then rotate entire environment
   hdrgen -p sun-sky --sun-azimuth 0 --rotation 90
   ```

2. **Use Multiples of 90° for Symmetry:**
   - 0°, 90°, 180°, 270° preserve cubemap alignment
   - Fractional angles may introduce minor artifacts

### Intensity Scaling

1. **Test Energy Conservation:**
   ```bash
   # White furnace at different intensities
   hdrgen -p white-furnace --intensity-scale 1.0 -o f1.hdr
   hdrgen -p white-furnace --intensity-scale 10.0 -o f10.hdr
   ```

2. **Match Real-World Values:**
   - Outdoor: scale 50-200 for direct sun
   - Indoor: scale 1-10 for artificial lights
   - Studio: scale 10-100 for key lights

### Tone Mapping

1. **Choose Method by Content:**
   - **Simple:** Low dynamic range, flat lighting
   - **Reinhard:** Balanced scenes with moderate highlights
   - **ACES:** High dynamic range, cinematic look

2. **Adjust Exposure First:**
   ```bash
   # Start with neutral exposure
   hdrgen -p sun-sky -f png --exposure 0.0
   # Adjust if too bright/dark
   hdrgen -p sun-sky -f png --exposure -1.0
   ```

3. **Use Consistent Gamma:**
   - sRGB displays: `--gamma 2.2` (default)
   - Mac displays: `--gamma 1.8`
   - Linear workflow: `--gamma 1.0`

---

## 🐛 Known Limitations

1. **PNG Compression:**
   - Current implementation uses uncompressed PNG
   - File sizes larger than library-encoded PNG
   - Consider using `sharp` or `pngjs` for production

2. **JPEG Support:**
   - Currently converts to BMP
   - Requires `jpeg-js` library for true JPEG

3. **Rotation Quality:**
   - Uses bilinear filtering (good quality)
   - Large rotations (>45°) may show minor softening
   - Consider rotating less and adjusting sun azimuth instead

4. **Memory Usage:**
   - Rotation duplicates image in memory
   - 4K images require ~200MB RAM during rotation
   - Close other applications if generating very large images

---

## 📚 Further Reading

- [Tone Mapping Operators](https://en.wikipedia.org/wiki/Tone_mapping)
- [ACES Filmic Tone Mapping](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)
- [Reinhard Tone Mapping](http://www.cmap.polytechnique.fr/~peyre/cours/x2005signal/hdr_photographic.pdf)
- [Gamma Correction](https://en.wikipedia.org/wiki/Gamma_correction)

---

## 📞 Support

For issues or questions about new features:
- Check examples in `examples/` directory
- Read [CHANGELOG.md](./CHANGELOG.md)
- See main [README.md](./README.md)
- Report bugs: https://github.com/syoyo/tinyusdz

---

## 🎉 What's Next?

Future enhancements being considered:
- True JPEG encoding (via jpeg-js)
- Compressed PNG output (via pngjs)
- Flip horizontal/vertical
- Crop and resize operations
- Batch processing mode
- Animation sequences (time-of-day)
- Real-time preview server

---

**Version:** 1.1.0
**Date:** 2025-11-06
**Author:** TinyUSDZ Project
