# Environment Maps

This directory contains synthetic HDR environment maps in lat-long (equirectangular) format, generated using the HDRGen tool (`tools/hdrgen/`).

## Available Environment Maps

All maps are **512x256** resolution (2:1 aspect ratio), **Radiance RGBE (.hdr)** format, ~513KB each.

### Testing Environment

| File | Description | Use Case |
|------|-------------|----------|
| `env_white_furnace.hdr` | Uniform white environment (intensity: 1.0) | Energy conservation testing, BRDF validation |

**Properties:**
- Intensity: 1.0 (uniform across entire environment)
- Perfect for validating that materials integrate to expected values
- Diffuse white material should appear with color = (1, 1, 1)

---

### Outdoor Environments (Sun & Sky)

#### Afternoon
**File:** `env_sunsky_afternoon.hdr`

**Properties:**
- Sun elevation: 45° (mid-height)
- Sun azimuth: 135° (southeast)
- Sun intensity: 100
- Sky intensity: 0.5
- Character: Balanced natural lighting

**Use Cases:**
- General outdoor scenes
- Product visualization
- Character lighting
- Natural material testing

---

#### Sunset
**File:** `env_sunsky_sunset.hdr`

**Properties:**
- Sun elevation: 5° (low on horizon)
- Sun azimuth: 270° (west)
- Sun intensity: 150 (warm, intense)
- Sky intensity: 0.3 (dimmer ambient)
- Character: Dramatic, warm, long shadows

**Use Cases:**
- Dramatic lighting scenarios
- Warm color temperature testing
- Long shadow rendering
- Golden hour visualization

---

#### Noon
**File:** `env_sunsky_noon.hdr`

**Properties:**
- Sun elevation: 85° (nearly overhead)
- Sun azimuth: 0° (north)
- Sun intensity: 200 (very bright)
- Sky intensity: 0.8 (bright ambient)
- Character: Harsh, overhead lighting

**Use Cases:**
- High-intensity material testing
- Overhead lighting scenarios
- Strong contrast testing
- High dynamic range validation

---

### Studio Environments (3-Point Lighting)

All studio environments use professional 3-point lighting:
- **Key light:** Main light (front-right, elevated, warm)
- **Fill light:** Shadow fill (front-left, lower, cool)
- **Rim light:** Edge separation (back, elevated, neutral)
- **Ambient:** Overall base lighting

#### Default Studio
**File:** `env_studio_default.hdr`

**Properties:**
- Key intensity: 50
- Fill intensity: 10
- Rim intensity: 20
- Ambient intensity: 0.5
- Character: Balanced, professional

**Use Cases:**
- Standard product renders
- General material preview
- Balanced lighting tests
- Reference renders

---

#### High-Key Studio
**File:** `env_studio_highkey.hdr`

**Properties:**
- Key intensity: 80 (brighter)
- Fill intensity: 30 (more fill)
- Rim intensity: 10 (less rim)
- Ambient intensity: 1.0 (higher ambient)
- Character: Bright, soft, low contrast

**Use Cases:**
- Product photography style
- Light-colored materials
- Beauty/cosmetic rendering
- Soft lighting scenarios

---

#### Low-Key Studio
**File:** `env_studio_lowkey.hdr`

**Properties:**
- Key intensity: 30 (dimmer)
- Fill intensity: 5 (minimal fill)
- Rim intensity: 40 (strong rim)
- Ambient intensity: 0.1 (very low ambient)
- Character: Dramatic, high contrast, dark

**Use Cases:**
- Dramatic product shots
- Dark materials testing
- Edge lighting emphasis
- Cinematic/moody renders

---

## Usage in USD Files

### Direct Reference

```usda
def DomeLight "EnvLight"
{
    asset inputs:texture:file = @./textures/env_sunsky_afternoon.hdr@
    float inputs:intensity = 1.0
}
```

### With Three.js

```javascript
const loader = new THREE.RGBELoader();
loader.load('models/textures/env_sunsky_afternoon.hdr', (texture) => {
    texture.mapping = THREE.EquirectangularReflectionMapping;
    scene.environment = texture;
    scene.background = texture;
});
```

### With Blender

1. Switch to **Shading** workspace
2. Select **World** in shader editor
3. Add **Environment Texture** node
4. Load HDR file
5. Connect to **Background** shader

---

## Technical Specifications

### Format Details

- **Format:** Radiance RGBE (.hdr)
- **Resolution:** 512 x 256 pixels
- **Aspect Ratio:** 2:1 (lat-long/equirectangular)
- **Projection:** Equirectangular (lat-long)
- **Color Space:** Linear RGB (no gamma encoding)
- **Dynamic Range:** HDR (values > 1.0 preserved)
- **File Size:** ~513 KB per file

### Coordinate System

- **U axis (horizontal):** 0 = west, 0.5 = north, 1.0 = east
- **V axis (vertical):** 0 = bottom (-90°), 0.5 = horizon, 1.0 = top (+90°)
- **Up direction:** +Y

### Intensity Ranges

| Environment Type | Typical Range | Peak Values |
|-----------------|---------------|-------------|
| White Furnace | 1.0 | 1.0 |
| Sun Disk | 100-200 | 200+ |
| Sky | 0.3-0.8 | 2.0 |
| Studio Key | 30-80 | 100 |
| Studio Fill | 5-30 | 50 |
| Studio Rim | 10-40 | 60 |

---

## Reproducing Environment Maps

All environment maps in this directory can be regenerated using the provided shell script:

```bash
# From project root
bash models/textures/generate_envmaps.sh

# Or from this directory
cd models/textures
bash generate_envmaps.sh
```

**What the script does:**
- Checks for Node.js 16+ and HDRGen tool availability
- Generates all 7 environment map presets with exact parameters
- Outputs 512×256 HDR files (~513 KB each)
- Optionally generates PNG previews with tone mapping

**Requirements:**
- Node.js 16.0.0 or higher
- HDRGen tool installed at `tools/hdrgen/`

**Script location:** `models/textures/generate_envmaps.sh`

---

## Generating Custom Environments

These environments were generated using the HDRGen tool. To create custom variations:

```bash
cd tools/hdrgen

# Custom sun position
node src/cli.js -p sun-sky \
  --sun-elevation 30 \
  --sun-azimuth 180 \
  --sun-intensity 120 \
  -w 512 --height 256 \
  -o ../../models/textures/env_custom.hdr

# Custom studio lighting
node src/cli.js -p studio \
  --key-intensity 60 \
  --fill-intensity 15 \
  --rim-intensity 30 \
  -w 512 --height 256 \
  -o ../../models/textures/env_custom_studio.hdr

# Rotated environment
node src/cli.js -p sun-sky \
  --rotation 45 \
  --intensity-scale 1.5 \
  -w 512 --height 256 \
  -o ../../models/textures/env_custom_rotated.hdr
```

See `tools/hdrgen/README.md` for complete documentation.

---

## Resolution Guidelines

**Current (512x256):**
- File size: ~513 KB
- Good for: Development, previews, testing
- Performance: Fast load times
- Quality: Good for most use cases

**If you need higher resolution:**

```bash
# 1K (1024x512) - ~2 MB
node src/cli.js -p sun-sky -w 1024 --height 512 -o env_1k.hdr

# 2K (2048x1024) - ~8 MB
node src/cli.js -p sun-sky -w 2048 --height 1024 -o env_2k.hdr

# 4K (4096x2048) - ~32 MB
node src/cli.js -p sun-sky -w 4096 --height 2048 -o env_4k.hdr
```

---

## LDR Previews

Generate web-friendly PNG previews:

```bash
cd tools/hdrgen

# PNG preview with tone mapping
node src/cli.js -p sun-sky \
  -f png \
  --exposure 0.5 \
  --tonemap-method reinhard \
  -w 512 --height 256 \
  -o ../../models/textures/env_sunsky_afternoon.png
```

---

## Testing & Validation

### Energy Conservation Test

Use `env_white_furnace.hdr` with a perfectly diffuse white material:

**Expected Result:**
- Material should appear white (R=1, G=1, B=1)
- No color shift
- Uniform brightness across surface
- No hotspots or dark areas

**Interpretation:**
- If material appears darker: BRDF is absorbing energy (incorrect)
- If material appears brighter: BRDF is adding energy (incorrect)
- If material appears colored: Color channels not balanced

### HDR Range Test

Use high-intensity environments (sunset, noon) to validate:
- Proper tone mapping
- Highlight preservation
- No clipping artifacts
- Smooth gradation in bright areas

---

## DCC Compatibility

These environment maps work in:

- ✅ **Blender** - World environment texture
- ✅ **Houdini** - Environment light
- ✅ **Maya** - Skydome light
- ✅ **Unreal Engine** - Skylight cubemap
- ✅ **Unity** - Skybox texture
- ✅ **Three.js** - RGBELoader → scene.environment
- ✅ **Arnold** - Skydome light
- ✅ **V-Ray** - Dome light
- ✅ **Cycles** - Environment texture

---

## Notes

1. **Linear Color Space:** All environments are in linear RGB. No sRGB gamma encoding applied.

2. **HDR Values:** Pixel values can exceed 1.0. This is intentional and required for proper PBR rendering.

3. **Rotation:** If lighting direction needs adjustment, use the `--rotation` flag when generating:
   ```bash
   node src/cli.js -p sun-sky --rotation 90 -o env_rotated.hdr
   ```

4. **Intensity Scaling:** To adjust overall brightness:
   ```bash
   node src/cli.js -p studio --intensity-scale 2.0 -o env_bright.hdr
   ```

5. **File Naming:** Prefix `env_` distinguishes environment maps from texture maps in this directory.

---

## Related Files

- **HDRGen Tool:** `tools/hdrgen/` - Environment map generator
- **HDRGen README:** `tools/hdrgen/README.md` - Complete documentation
- **OpenPBR Test Scenes:** `models/openpbr-*.usda` - Use these environments

---

## Changelog

**2025-11-06:** Initial set of 7 environment maps generated
- White furnace (testing)
- Sun/sky: afternoon, sunset, noon
- Studio: default, high-key, low-key
- All at 512x256 resolution
- Total: ~3.6 MB for all 7 files

---

Generated with [HDRGen](../../tools/hdrgen/) v1.1.0
