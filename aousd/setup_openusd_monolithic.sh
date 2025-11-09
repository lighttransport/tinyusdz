#!/bin/bash

# OpenUSD Monolithic Build Setup Script for comparison with TinyUSDZ
# This script clones, builds, and installs OpenUSD as a single monolithic library
# with Python bindings

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENUSD_DIR="${SCRIPT_DIR}/OpenUSD"
DIST_DIR="${SCRIPT_DIR}/dist_monolithic"
VENV_DIR="${SCRIPT_DIR}/venv"

echo "================================================"
echo "OpenUSD Monolithic Build Setup Script"
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

# Step 2: Setup Python 3.11 with uv (reuse existing venv if available)
echo ""
echo "Step 2: Setting up Python 3.11 with uv..."
if ! command -v uv &> /dev/null; then
    echo "Installing uv..."
    curl -LsSf https://astral.sh/uv/install.sh | sh
    # Add uv to PATH for current session
    export PATH="$HOME/.cargo/bin:$PATH"
fi

# Create virtual environment with Python 3.11 if it doesn't exist
if [ ! -d "${VENV_DIR}" ]; then
    echo "Creating virtual environment with Python 3.11..."
    cd "${SCRIPT_DIR}"
    uv venv "${VENV_DIR}" --python 3.11
else
    echo "Using existing virtual environment..."
fi

# Activate virtual environment
echo "Activating virtual environment..."
source "${VENV_DIR}/bin/activate"

# Step 3: Install Python dependencies
echo ""
echo "Step 3: Installing Python dependencies..."
uv pip install PyOpenGL PySide2 numpy cmake>=3.26

# Step 4: Setup compilers
echo ""
echo "Step 4: Setting up C/C++ compilers..."

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

# Step 5: Build OpenUSD with Monolithic option
echo ""
echo "Step 5: Building OpenUSD with MONOLITHIC option..."
echo "This may take a while..."

cd "${OPENUSD_DIR}"

# Prepare build arguments for monolithic build with Python support
BUILD_ARGS=(
    "${DIST_DIR}"
    --no-tests
    --no-examples
    --no-tutorials
    --no-tools
    --no-docs
    --python
    --no-imaging
    --no-usdview
    --no-materialx
    --no-embree
    --no-ptex
    --no-openvdb
    --no-draco
    --build-args
    "USD,\"-DPXR_BUILD_MONOLITHIC=ON\""
)

echo "Build configuration:"
echo "  - Installation directory: ${DIST_DIR}"
echo "  - Python bindings: ON"
echo "  - Monolithic build: ON (single shared library)"
echo "  - Minimal dependencies (no imaging, materialx, etc.)"
echo ""

# Run build script
python build_scripts/build_usd.py "${BUILD_ARGS[@]}"

# Step 6: Create environment setup script
echo ""
echo "Step 6: Creating environment setup script..."

cat > "${SCRIPT_DIR}/setup_env_monolithic.sh" << 'EOF'
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
EOF

chmod +x "${SCRIPT_DIR}/setup_env_monolithic.sh"

echo ""
echo "================================================"
echo "OpenUSD Monolithic build completed successfully!"
echo "================================================"
echo ""
echo "Installation directory: ${DIST_DIR}"
echo "Build type: Monolithic (single shared library)"
echo ""
echo "To use OpenUSD tools and Python bindings, run:"
echo "  source ${SCRIPT_DIR}/setup_env_monolithic.sh"
echo ""
echo "Then you can use commands like:"
echo "  - usdcat <file.usd>"
echo "  - python -c 'from pxr import Usd'"
echo "================================================"
