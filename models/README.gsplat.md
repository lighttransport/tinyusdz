# Gaussian Splat File Support

TinyUSDZ supports loading Gaussian splat files in both SPZ and PLY formats:
- **SPZ**: Niantic Labs compressed format (~10x smaller than PLY)
- **PLY**: Standard PLY format with Gaussian splat data (binary_little_endian)

## Obtaining Gaussian Splat Files

### Option 1: Scaniverse App (.spz)
The easiest way to create .spz files is using the [Scaniverse](https://scaniverse.com) iOS app:
1. Download Scaniverse from the App Store
2. Capture a 3D scene using your iPhone/iPad
3. Export as "Gaussian Splat (.spz)"

### Option 2: 3D Gaussian Splatting Training (.ply)
Train Gaussian splats from images using the original research code:
```bash
git clone https://github.com/graphdeco-inria/gaussian-splatting.git
cd gaussian-splatting
# Follow training instructions
# Output will be in output/<scene>/point_cloud/iteration_*/point_cloud.ply
```

### Option 3: Convert between formats
Convert between .ply and .spz using the SPZ tools:

```bash
# Clone the SPZ repository
git clone https://github.com/nianticlabs/spz.git
cd spz

# Build the converter
mkdir build && cd build
cmake ..
make

# Convert .ply to .spz (compress)
./ply2spz input.ply output.spz

# Convert .spz to .ply (decompress)
./spz2ply input.spz output.ply
```

### Option 4: Download Sample Data
Sample files may be available from:
- [Niantic Labs SPZ repository](https://github.com/nianticlabs/spz) samples
- [3D Gaussian Splatting](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/) research datasets
- Community-shared captures on PolyHaven, Sketchfab, etc.

## Using Gaussian Splat Files in USD

### Method 1: Reference in USD File
Both .spz and .ply files can be referenced directly:

```usda
# Reference .spz file
def Points "MySplat_SPZ" (
    prepend references = @model.spz@
) {
    # .spz data automatically loaded as primvars:gsplat:*
}

# Reference .ply file
def Points "MySplat_PLY" (
    prepend references = @model.ply@
) {
    # .ply data automatically loaded as primvars:gsplat:*
}
```

### Method 2: Direct Loading (C++ API)
```cpp
#include "usdGSplat.hh"

using namespace tinyusdz;

GeomPoints points;
usdGSplat::LoadOptions options;
options.targetCoordinateSystem = usdGSplat::LoadOptions::CoordinateSystem::RUB;

// Load .spz file
if (usdGSplat::ReadSPZFromFile("model.spz", &points, options)) {
    // points now contains Gaussian splat data
}

// Or load .ply file
if (usdGSplat::ReadPLYFromFile("model.ply", &points, options)) {
    // points now contains Gaussian splat data
}
```

## Format Details

### SPZ Format (Compressed)
The .spz format stores Gaussian splats with:
- **Positions**: 3D position of each gaussian (float3)
- **Scales**: Log-scale size in 3 dimensions (float3)
- **Rotations**: Quaternion orientation (quatf, smallest-three encoding)
- **Alphas**: Opacity values 0-1 (float, inverse logistic encoding)
- **Colors**: Spherical harmonics DC component (float3)
- **SH Coefficients**: Optional higher-order SH for view-dependent color (float3[] x degree)

**Advantages**:
- Files are typically **10x smaller** than equivalent .ply files
- Minimal quality loss (uses quantization and compression)
- Supports antialiasing (mip-splatting)
- Binary format with gzip compression
- Each splat: ~64 bytes (vs 236 bytes in PLY)

### PLY Format (Uncompressed)
Standard PLY format with Gaussian splat vertex properties:
- **Format**: `binary_little_endian 1.0`
- **x, y, z**: Position coordinates
- **scale_0, scale_1, scale_2**: Gaussian scale (log-scale)
- **rot_0, rot_1, rot_2, rot_3**: Quaternion rotation (x, y, z, w)
- **opacity**: Opacity value (pre-sigmoid)
- **f_dc_0, f_dc_1, f_dc_2**: Spherical harmonics DC component (RGB)
- **f_rest_0** to **f_rest_44**: Higher-order SH coefficients (optional)

**Advantages**:
- Widely supported format
- Human-readable ASCII header
- Compatible with 3D Gaussian Splatting training output
- No compression artifacts

## Coordinate Systems

SPZ supports multiple coordinate systems. TinyUSDZ defaults to RUB (OpenGL/Three.js convention):
- **RUB**: Right Up Back (OpenGL, Three.js) - default
- **RDF**: Right Down Forward (PLY format)
- **LUF**: Left Up Forward (glTF)
- **RUF**: Right Up Forward (Unity)

Specify coordinate system when loading:
```cpp
options.targetCoordinateSystem = usdGSplat::LoadOptions::CoordinateSystem::RDF;
```

## License

The SPZ format and library are provided under the MIT License by Niantic Labs.
See `src/external/spz/LICENSE.spz` for details.
