#!/bin/bash

# OpenUSD No-Python Build Setup Script for comparison with TinyUSDZ
# This script clones, builds, and installs OpenUSD WITHOUT Python bindings
# Useful for C++-only applications and minimal deployments

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENUSD_DIR="${SCRIPT_DIR}/OpenUSD"
DIST_DIR="${SCRIPT_DIR}/dist_nopython"

echo "================================================"
echo "OpenUSD No-Python Build Setup Script"
echo "================================================"
echo "Working directory: ${SCRIPT_DIR}"
echo ""

# Step 1: Clone OpenUSD repository
echo "Step 1: Cloning OpenUSD repository (release branch)..."
if [ -d "${OPENUSD_DIR}" ]; then
    echo "OpenUSD directory already exists. Pulling latest changes..."
    cd "${OPENUSD_DIR}"
    git fetch origin
    git checkout release
    git pull origin release
else
    git clone -b release https://github.com/lighttransport/OpenUSD.git "${OPENUSD_DIR}"
fi

# Step 2: Setup compilers
echo ""
echo "Step 2: Setting up C/C++ compilers..."

# Allow user to override compilers via environment variables
# If not set, try to find suitable defaults
if [ -z "$CC" ]; then
    if command -v gcc &> /dev/null; then
        export CC=gcc
    elif command -v clang &> /dev/null; then
        export CC=clang
    else
        echo "Error: No C compiler found. Please install gcc or clang."
        exit 1
    fi
fi

if [ -z "$CXX" ]; then
    if command -v g++ &> /dev/null; then
        export CXX=g++
    elif command -v clang++ &> /dev/null; then
        export CXX=clang++
    else
        echo "Error: No C++ compiler found. Please install g++ or clang++."
        exit 1
    fi
fi

echo "Using C compiler: $CC"
echo "Using C++ compiler: $CXX"

# Step 3: Build OpenUSD without Python
echo ""
echo "Step 3: Building OpenUSD without Python bindings..."
echo "This may take a while..."

cd "${OPENUSD_DIR}"

# Prepare build arguments for no-python build
BUILD_ARGS=(
    "${DIST_DIR}"
    --no-tests
    --no-examples
    --no-tutorials
    --no-tools
    --no-docs
    --no-python
    --no-imaging
    --no-usdview
    --no-materialx
    --no-embree
    --no-ptex
    --no-openvdb
    --no-draco
    --build-args
    "USD,\"-DPXR_BUILD_ALEMBIC_PLUGIN=OFF\""
)

echo "Build configuration:"
echo "  - Installation directory: ${DIST_DIR}"
echo "  - Python bindings: OFF"
echo "  - Minimal dependencies (no imaging, materialx, etc.)"
echo "  - C++ libraries and headers only"
echo ""

# Run build script
python3 build_scripts/build_usd.py "${BUILD_ARGS[@]}"

# Step 4: Create environment setup script
echo ""
echo "Step 4: Creating environment setup script..."

cat > "${SCRIPT_DIR}/setup_env_nopython.sh" << 'EOF'
#!/bin/bash

# OpenUSD No-Python Build Environment Setup Script
# Source this script to set up the environment for using OpenUSD C++ libraries and tools

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
USD_INSTALL_ROOT="${SCRIPT_DIR}/dist_nopython"

if [ ! -d "${USD_INSTALL_ROOT}" ]; then
    echo "Error: OpenUSD no-python installation not found at ${USD_INSTALL_ROOT}"
    echo "Please run setup_openusd_nopython.sh first."
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
echo "OpenUSD No-Python environment configured!"
echo "================================================"
echo "USD_INSTALL_ROOT: ${USD_INSTALL_ROOT}"
echo "Build type: C++-only (no Python bindings)"
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
echo "================================================"
EOF

chmod +x "${SCRIPT_DIR}/setup_env_nopython.sh"

echo ""
echo "================================================"
echo "OpenUSD No-Python build completed successfully!"
echo "================================================"
echo ""
echo "Installation directory: ${DIST_DIR}"
echo "Build type: C++-only (no Python bindings)"
echo ""
echo "To use OpenUSD tools and C++ libraries, run:"
echo "  source ${SCRIPT_DIR}/setup_env_nopython.sh"
echo ""
echo "Then you can:"
echo "  - Use command-line tools: usdcat <file.usd>"
echo "  - Link against C++ libraries in your projects"
echo "================================================"
