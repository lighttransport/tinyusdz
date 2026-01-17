# HDRGen Quick Start Guide

## Installation

```bash
cd tools/hdrgen
npm install  # (No dependencies currently)
```

## Generate Your First Environment Map

### 1. White Furnace (Testing)

Perfect uniform white environment for energy conservation testing:

```bash
node src/cli.js --preset white-furnace -o output/furnace.hdr
```

Output: `output/furnace.hdr` (2048x1024, ~8MB)

### 2. Sun & Sky (Outdoor)

Procedural outdoor environment with sun:

```bash
node src/cli.js --preset sun-sky --sun-elevation 45 --sun-azimuth 135 -o output/sky.hdr
```

Output: `output/sky.hdr` (2048x1024, ~8MB)

**Tip:** Adjust sun position:
- `--sun-elevation 5`: Sunset (low sun)
- `--sun-elevation 85`: Noon (overhead)
- `--sun-azimuth 90`: East
- `--sun-azimuth 270`: West

### 3. Studio Lighting (Indoor)

Professional 3-point lighting setup:

```bash
node src/cli.js --preset studio -o output/studio.hdr
```

Output: `output/studio.hdr` (2048x1024, ~8MB)

## Common Options

```bash
# Custom resolution
node src/cli.js -p sun-sky -w 4096 --height 2048 -o output/sky_4k.hdr

# Generate cubemap (6 faces)
node src/cli.js -p studio --projection cubemap --width 512 -o output/studio_cube
# Creates: studio_cube_+X.hdr, studio_cube_-X.hdr, ... (6 files)

# Adjust intensity
node src/cli.js -p sun-sky --sun-intensity 200 --sky-intensity 0.8 -o output/bright_sky.hdr
```

## Generate All Examples

```bash
npm run example
```

This generates 7+ example environment maps in `output/`:
- White furnace
- Sun/sky variations (afternoon, sunset, noon)
- Studio lighting variations (default, high-key, low-key)
- Cubemap example

## View Generated HDR Files

**On macOS/Linux:**
```bash
# Install ImageMagick
sudo apt install imagemagick  # Ubuntu/Debian
brew install imagemagick      # macOS

# Convert HDR to viewable PNG
convert output/furnace.hdr output/furnace.png
```

**In Blender:**
1. Switch to **Shading** workspace
2. Select **World** shader
3. Add **Environment Texture** node
4. Open your `.hdr` file

**In Web Browser:**
Use online HDR viewer: https://www.hdrlabs.com/sibl/viewer.html

## Quick Command Reference

| Command | Description |
|---------|-------------|
| `-p, --preset` | Preset name: white-furnace, sun-sky, studio |
| `-w, --width` | Width in pixels (default: 2048) |
| `--height` | Height in pixels (default: 1024) |
| `--projection` | latlong or cubemap (default: latlong) |
| `-f, --format` | hdr or exr (default: hdr) |
| `-o, --output` | Output file path |
| `--sun-elevation` | Sun angle above horizon (0-90°) |
| `--sun-azimuth` | Sun compass direction (0-360°) |
| `--key-intensity` | Studio key light intensity |

For full documentation, see [README.md](./README.md)

## Troubleshooting

### Files too dark/bright?

Adjust intensity parameters:
```bash
# Brighter sun
node src/cli.js -p sun-sky --sun-intensity 200

# Dimmer studio
node src/cli.js -p studio --key-intensity 25
```

### Sun in wrong position?

Check azimuth (compass direction):
- 0° = North (center top of image)
- 90° = East (right side)
- 180° = South (center bottom)
- 270° = West (left side)

### Need help?

```bash
node src/cli.js --help
```

## Next Steps

- Read full [README.md](./README.md) for detailed documentation
- Check [examples/](./examples/) directory for code samples
- Import generated HDR files into your DCC (Blender, Houdini, etc.)
- Use for IBL testing in your renderer

## File Sizes

| Resolution | File Size (HDR) | Use Case |
|-----------|----------------|----------|
| 512x256 | ~0.5 MB | Quick preview |
| 1024x512 | ~2 MB | Development |
| 2048x1024 | ~8 MB | Production (standard) |
| 4096x2048 | ~32 MB | Production (high quality) |

Cubemaps are ~6x the equivalent lat-long size (one file per face).
