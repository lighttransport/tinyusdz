#!/bin/bash
# LightUSD WASM debug build script
# Requires: Emscripten SDK installed and activated

set -e

rm -rf build
mkdir build

emcmake cmake -DCMAKE_BUILD_TYPE=Debug -DLIGHTUSD_WASM_DEBUG=ON -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild
cmake --build build -j$(nproc)

echo ""
echo "Debug build complete! Output files:"
ls -lh js/src/lightusd/
