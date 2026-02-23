#!/bin/bash
# LightUSD WASM build script (Release/MinSizeRel)
# Requires: Emscripten SDK installed and activated

set -e

rm -rf build
mkdir build

emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild
cmake --build build -j$(nproc)

echo ""
echo "Build complete! Output files:"
ls -lh js/src/lightusd/
