#!/bin/bash

# Build script for CMake-based USD comparison tool

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building USD Comparison Tool with CMake${NC}"
echo "========================================="

# Create build directory
if [ ! -d "build" ]; then
    mkdir build
fi

cd build

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
CC=clang-20 CXX=clang++-20 cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENUSD_ROOT=../../dist \
    -DTINYUSDZ_ROOT=../../.. \
    -DTINYUSDZ_BUILD=../../../build

# Build
echo -e "${YELLOW}Building...${NC}"
make -j$(nproc)

echo -e "${GREEN}Build complete!${NC}"
echo ""
echo "To run the application:"
echo "  cd build"
echo "  LD_LIBRARY_PATH=../../dist/lib ./usd_comparison [usd_file]"
echo ""
echo "Or use: make run"