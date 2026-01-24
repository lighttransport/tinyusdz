#!/bin/bash
# SPDX-License-Identifier: Apache 2.0
#
# Build script for TinyUSDZ Python ABI3 binding

set -e  # Exit on error

echo "========================================"
echo "TinyUSDZ ABI3 Binding Build Script"
echo "========================================"
echo

# Check Python version
PYTHON_VERSION=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
PYTHON_MINOR=$(python3 -c 'import sys; print(sys.version_info.minor)')

echo "Python version: $PYTHON_VERSION"

if [ "$PYTHON_MINOR" -lt 10 ]; then
    echo "Error: Python 3.10+ is required"
    echo "Current version: $PYTHON_VERSION"
    exit 1
fi

# Determine build method
BUILD_METHOD="${1:-setup}"

case "$BUILD_METHOD" in
    setup)
        echo
        echo "Building with setup.py..."
        echo "----------------------------------------"
        python3 setup.py build_ext --inplace
        echo
        echo "Build complete!"
        echo
        echo "The module is now available as: tinyusdz_abi3.so (or .pyd on Windows)"
        echo
        echo "Try it out:"
        echo "  python3 examples/example_basic.py"
        echo "  python3 tests/test_basic.py"
        ;;

    wheel)
        echo
        echo "Building wheel..."
        echo "----------------------------------------"
        python3 setup.py bdist_wheel
        echo
        echo "Wheel created in dist/"
        ls -lh dist/*.whl
        echo
        echo "Install with:"
        echo "  pip install dist/tinyusdz_abi3-*.whl"
        ;;

    cmake)
        echo
        echo "Building with CMake..."
        echo "----------------------------------------"
        mkdir -p build
        cd build
        cmake ..
        make -j$(nproc 2>/dev/null || echo 4)
        cd ..
        echo
        echo "Build complete!"
        echo "The module is in: build/tinyusdz_abi3.so"
        echo
        echo "Copy it to the current directory to use:"
        echo "  cp build/tinyusdz_abi3.so ."
        ;;

    clean)
        echo
        echo "Cleaning build artifacts..."
        echo "----------------------------------------"
        rm -rf build dist *.egg-info
        rm -f tinyusdz_abi3.so tinyusdz_abi3.*.so
        rm -f tinyusdz_abi3.pyd tinyusdz_abi3.*.pyd
        find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
        echo "Clean complete!"
        ;;

    *)
        echo "Usage: $0 [setup|wheel|cmake|clean]"
        echo
        echo "Build methods:"
        echo "  setup  - Build in-place with setup.py (default)"
        echo "  wheel  - Build wheel distribution"
        echo "  cmake  - Build with CMake"
        echo "  clean  - Remove build artifacts"
        exit 1
        ;;
esac

echo
echo "========================================"
