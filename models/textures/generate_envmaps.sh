#!/bin/bash
#
# Generate all environment map presets for MaterialX testing
#
# This script reproduces all the environment maps in this directory.
# Requires Node.js 16+ and the HDRGen tool in tools/hdrgen/
#
# Usage:
#   cd models/textures
#   bash generate_envmaps.sh
#
# Or from project root:
#   bash models/textures/generate_envmaps.sh
#

set -e  # Exit on error

# Determine script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HDRGEN="$PROJECT_ROOT/tools/hdrgen/src/cli.js"
OUTPUT_DIR="$SCRIPT_DIR"

# Check if HDRGen tool exists
if [ ! -f "$HDRGEN" ]; then
    echo "Error: HDRGen tool not found at $HDRGEN"
    echo "Please ensure tools/hdrgen/ is properly set up."
    exit 1
fi

# Check Node.js version
NODE_VERSION=$(node --version | cut -d'v' -f2 | cut -d'.' -f1)
if [ "$NODE_VERSION" -lt 16 ]; then
    echo "Error: Node.js 16+ required (found: $(node --version))"
    exit 1
fi

echo "=========================================="
echo "Generating Environment Map Presets"
echo "=========================================="
echo "Output directory: $OUTPUT_DIR"
echo "HDRGen tool: $HDRGEN"
echo ""

# Common parameters
WIDTH=512
HEIGHT=256
FORMAT="hdr"

# =============================================================================
# Testing Environment
# =============================================================================

echo "[1/7] Generating White Furnace (testing)..."
node "$HDRGEN" \
  --preset white-furnace \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_white_furnace.hdr"

echo "✓ env_white_furnace.hdr"
echo ""

# =============================================================================
# Sun & Sky Environments
# =============================================================================

echo "[2/7] Generating Sun/Sky - Afternoon..."
node "$HDRGEN" \
  --preset sun-sky \
  --sun-elevation 45 \
  --sun-azimuth 135 \
  --sun-intensity 100 \
  --sky-intensity 0.5 \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_sunsky_afternoon.hdr"

echo "✓ env_sunsky_afternoon.hdr (Sun: 45° elev, 135° azimuth)"
echo ""

echo "[3/7] Generating Sun/Sky - Sunset..."
node "$HDRGEN" \
  --preset sun-sky \
  --sun-elevation 5 \
  --sun-azimuth 270 \
  --sun-intensity 150 \
  --sky-intensity 0.3 \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_sunsky_sunset.hdr"

echo "✓ env_sunsky_sunset.hdr (Sun: 5° elev, 270° azimuth, warm/dramatic)"
echo ""

echo "[4/7] Generating Sun/Sky - Noon..."
node "$HDRGEN" \
  --preset sun-sky \
  --sun-elevation 85 \
  --sun-azimuth 0 \
  --sun-intensity 200 \
  --sky-intensity 0.8 \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_sunsky_noon.hdr"

echo "✓ env_sunsky_noon.hdr (Sun: 85° elev, overhead, intense)"
echo ""

# =============================================================================
# Studio Lighting Environments
# =============================================================================

echo "[5/7] Generating Studio - Default..."
node "$HDRGEN" \
  --preset studio \
  --key-intensity 50 \
  --fill-intensity 10 \
  --rim-intensity 20 \
  --ambient-intensity 0.5 \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_studio_default.hdr"

echo "✓ env_studio_default.hdr (Key:50, Fill:10, Rim:20, Ambient:0.5)"
echo ""

echo "[6/7] Generating Studio - High-Key..."
node "$HDRGEN" \
  --preset studio \
  --key-intensity 80 \
  --fill-intensity 30 \
  --rim-intensity 10 \
  --ambient-intensity 1.0 \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_studio_highkey.hdr"

echo "✓ env_studio_highkey.hdr (Key:80, Fill:30, Rim:10, Ambient:1.0)"
echo ""

echo "[7/7] Generating Studio - Low-Key..."
node "$HDRGEN" \
  --preset studio \
  --key-intensity 30 \
  --fill-intensity 5 \
  --rim-intensity 40 \
  --ambient-intensity 0.1 \
  --width $WIDTH \
  --height $HEIGHT \
  --format $FORMAT \
  --output "$OUTPUT_DIR/env_studio_lowkey.hdr"

echo "✓ env_studio_lowkey.hdr (Key:30, Fill:5, Rim:40, Ambient:0.1)"
echo ""

# =============================================================================
# Summary
# =============================================================================

echo "=========================================="
echo "Generation Complete!"
echo "=========================================="
echo ""
echo "Generated 7 environment maps:"
ls -lh "$OUTPUT_DIR"/env_*.hdr | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo "Total size: $(du -sh "$OUTPUT_DIR"/env_*.hdr | tail -1 | awk '{print $1}')"
echo ""
echo "See ENVMAPS_README.md for detailed specifications and usage."
echo ""

# =============================================================================
# Optional: Generate LDR Previews
# =============================================================================

read -p "Generate PNG previews (512x256)? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Generating PNG previews with tone mapping..."
    echo ""

    for hdr_file in "$OUTPUT_DIR"/env_*.hdr; do
        base_name=$(basename "$hdr_file" .hdr)
        png_file="$OUTPUT_DIR/${base_name}_preview.png"

        echo "  → ${base_name}_preview.png"
        node "$HDRGEN" \
          --preset sun-sky \
          --width $WIDTH \
          --height $HEIGHT \
          --format png \
          --tonemap-method reinhard \
          --exposure 0.0 \
          --gamma 2.2 \
          --output "$png_file" 2>/dev/null || true
    done

    echo ""
    echo "PNG previews generated!"
fi

echo ""
echo "Done!"
