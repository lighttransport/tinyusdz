# Synthetic Test Environment Maps

This directory contains synthetic environment maps in long-lat (equirectangular) format for testing purposes. All images are 128x64 HDR format (.hdr files using Radiance RGBE encoding).

## Generated Files

### RGB Axis Environment Map
- **env_synthetic_rgb_axes.hdr**
  - +X direction: Red
  - +Y direction: Green
  - +Z direction: Blue
  - -X, -Y, -Z directions: Black
  - Useful for testing coordinate system orientation and color channels

### Hemisphere Environment Map
- **env_synthetic_hemisphere_upper_white.hdr**
  - Upper hemisphere (+Y > 0): White
  - Lower hemisphere (+Y <= 0): Black
  - Useful for testing hemisphere lighting and ground plane interactions

### Single-Face White Environment Maps
Each of these maps has white color on one cube face direction, and black on all others:

- **env_synthetic_pos_x_red.hdr** - White on +X face only
- **env_synthetic_neg_x.hdr** - White on -X face only
- **env_synthetic_pos_y_green.hdr** - White on +Y face only
- **env_synthetic_neg_y.hdr** - White on -Y face only
- **env_synthetic_pos_z_blue.hdr** - White on +Z face only
- **env_synthetic_neg_z.hdr** - White on -Z face only

These are useful for testing individual cube face contributions and verifying environment map sampling directions.

## Generation

To regenerate these environment maps:

```bash
cd models/textures/test-envs
node generate_synthetic_envmaps.js
```

## Technical Details

- **Format**: Radiance RGBE HDR (.hdr)
- **Resolution**: 128x64 pixels
- **Projection**: Long-lat (equirectangular)
- **File size**: ~33 KB per file
- **Color space**: Linear (no gamma correction)

## Usage in Testing

These synthetic environment maps are ideal for:
- Verifying MaterialX environment shader implementations
- Testing IBL (Image-Based Lighting) rendering
- Debugging coordinate system transformations
- Validating cube map sampling and lookups
- Checking color channel assignments
