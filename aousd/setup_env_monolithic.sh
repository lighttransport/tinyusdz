#!/bin/bash

# OpenUSD Monolithic Build Environment Setup Script
# Source this script to set up the environment for using OpenUSD tools

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
USD_INSTALL_ROOT="${SCRIPT_DIR}/dist_monolithic"
VENV_DIR="${SCRIPT_DIR}/venv"

if [ ! -d "${USD_INSTALL_ROOT}" ]; then
    echo "Error: OpenUSD monolithic installation not found at ${USD_INSTALL_ROOT}"
    echo "Please run setup_openusd_monolithic.sh first."
    return 1
fi

# Activate Python virtual environment
if [ -f "${VENV_DIR}/bin/activate" ]; then
    source "${VENV_DIR}/bin/activate"
    echo "Python virtual environment activated."
else
    echo "Warning: Python virtual environment not found."
fi

# Set up USD environment variables
export USD_INSTALL_ROOT="${USD_INSTALL_ROOT}"
export PATH="${USD_INSTALL_ROOT}/bin:${PATH}"
export PYTHONPATH="${USD_INSTALL_ROOT}/lib/python:${PYTHONPATH}"

# Set library paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    export DYLD_LIBRARY_PATH="${USD_INSTALL_ROOT}/lib:${DYLD_LIBRARY_PATH}"
else
    # Linux
    export LD_LIBRARY_PATH="${USD_INSTALL_ROOT}/lib:${LD_LIBRARY_PATH}"
fi

echo "================================================"
echo "OpenUSD Monolithic environment configured!"
echo "================================================"
echo "USD_INSTALL_ROOT: ${USD_INSTALL_ROOT}"
echo "Python: $(which python)"
echo "Build type: Monolithic (single shared library)"
echo ""
echo "Available commands:"
echo "  - usdcat: Display USD files in text format"
echo "  - usddiff: Compare two USD files"
echo "  - usdtree: Display USD scene hierarchy"
echo "  - usdchecker: Validate USD files"
echo "  - usdzip: Create USDZ archives"
echo ""
echo "Python USD module:"
echo "  python -c 'from pxr import Usd; print(Usd)'"
echo "================================================"
