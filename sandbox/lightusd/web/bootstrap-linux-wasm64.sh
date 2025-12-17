#!/bin/bash
# LightUSD WASM64 build script (64-bit memory, for large files)
# Requires: Emscripten SDK installed and activated
# Note: WASM64 requires Chrome 133+ or recent Firefox

set -e

rm -rf build
mkdir build

emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DLIGHTUSD_WASM64=ON -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild
cmake --build build -j$(nproc)

echo ""
echo "WASM64 build complete! Output files:"
ls -lh js/src/lightusd/
