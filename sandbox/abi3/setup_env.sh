#!/bin/bash
# SPDX-License-Identifier: Apache 2.0
#
# Setup Python environment for TinyUSDZ ABI3 binding using uv
#
# This script:
# 1. Checks for uv installation
# 2. Creates a Python virtual environment
# 3. Installs required packages (numpy)
# 4. Builds the extension module
# 5. Runs tests and examples

set -e  # Exit on error

echo "========================================"
echo "TinyUSDZ ABI3 - Environment Setup"
echo "========================================"
echo

# Check if uv is installed
if ! command -v uv &> /dev/null; then
    echo "Error: uv is not installed"
    echo
    echo "Install uv with:"
    echo "  curl -LsSf https://astral.sh/uv/install.sh | sh"
    echo "  # or"
    echo "  pip install uv"
    echo
    exit 1
fi

echo "✓ Found uv: $(uv --version)"
echo

# Create virtual environment with uv
VENV_DIR=".venv"

if [ -d "$VENV_DIR" ]; then
    echo "Virtual environment already exists at $VENV_DIR"
    read -p "Recreate it? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Removing existing environment..."
        rm -rf "$VENV_DIR"
    else
        echo "Using existing environment"
    fi
fi

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment with uv..."
    uv venv "$VENV_DIR"
    echo "✓ Virtual environment created"
fi

echo

# Activate virtual environment
echo "Activating virtual environment..."
source "$VENV_DIR/bin/activate"
echo "✓ Environment activated"
echo

# Install numpy with uv pip
echo "Installing numpy with uv pip..."
uv pip install numpy
echo "✓ NumPy installed"
echo

# Install setuptools if needed (for building extension)
echo "Installing build dependencies..."
uv pip install setuptools wheel
echo "✓ Build dependencies installed"
echo

# Build the extension module
echo "========================================"
echo "Building Extension Module"
echo "========================================"
echo

python setup.py build_ext --inplace

if [ $? -eq 0 ]; then
    echo
    echo "✓ Extension module built successfully"
else
    echo
    echo "✗ Build failed"
    exit 1
fi

# Check if module can be imported
echo
echo "Testing module import..."
python -c "import tinyusdz_abi3; print('✓ Module import successful')" || {
    echo "✗ Module import failed"
    exit 1
}

echo

# Show installed packages
echo "========================================"
echo "Installed Packages"
echo "========================================"
uv pip list
echo

# Run tests if available
if [ -f "tests/test_basic.py" ]; then
    echo "========================================"
    echo "Running Tests"
    echo "========================================"
    echo
    python tests/test_basic.py
    echo
fi

# Run examples
if [ -f "examples/example_basic.py" ]; then
    echo "========================================"
    echo "Running Basic Example"
    echo "========================================"
    echo
    python examples/example_basic.py
    echo
fi

# Print usage instructions
echo "========================================"
echo "Setup Complete!"
echo "========================================"
echo
echo "Virtual environment is ready at: $VENV_DIR"
echo
echo "To activate the environment:"
echo "  source $VENV_DIR/bin/activate"
echo
echo "To run examples:"
echo "  python examples/example_basic.py"
echo "  python examples/example_numpy.py"
echo "  python examples/example_mesh_to_numpy.py [usd_file]"
echo
echo "To run tests:"
echo "  python tests/test_basic.py"
echo
echo "To deactivate:"
echo "  deactivate"
echo
echo "========================================"
