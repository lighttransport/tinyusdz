#!/bin/bash

# OpenUSD No-Python Monolithic Build Environment Setup Script
# Source this script to set up the environment for using OpenUSD C++ libraries and tools

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
USD_INSTALL_ROOT="${SCRIPT_DIR}/dist_nopython_monolithic"

if [ ! -d "${USD_INSTALL_ROOT}" ]; then
    echo "Error: OpenUSD no-python monolithic installation not found at ${USD_INSTALL_ROOT}"
    echo "Please run setup_openusd_nopython_monolithic.sh first."
    return 1
fi

# Set up USD environment variables
export USD_INSTALL_ROOT="${USD_INSTALL_ROOT}"
export PATH="${USD_INSTALL_ROOT}/bin:${PATH}"

# Set library paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    export DYLD_LIBRARY_PATH="${USD_INSTALL_ROOT}/lib:${DYLD_LIBRARY_PATH}"
else
    # Linux
    export LD_LIBRARY_PATH="${USD_INSTALL_ROOT}/lib:${LD_LIBRARY_PATH}"
fi

echo "================================================"
echo "OpenUSD No-Python Monolithic environment configured!"
echo "================================================"
echo "USD_INSTALL_ROOT: ${USD_INSTALL_ROOT}"
echo "Build type: Monolithic C++-only (single shared library, no Python)"
echo ""
echo "Available commands:"
echo "  - usdcat: Display USD files in text format"
echo "  - usddiff: Compare two USD files"
echo "  - usdtree: Display USD scene hierarchy"
echo "  - usdchecker: Validate USD files"
echo "  - usdzip: Create USDZ archives"
echo ""
echo "C++ Development:"
echo "  Include path: ${USD_INSTALL_ROOT}/include"
echo "  Library path: ${USD_INSTALL_ROOT}/lib"
echo "  Library name: libusd_ms.so (monolithic)"
echo "================================================"
