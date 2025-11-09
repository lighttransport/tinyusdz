#!/bin/bash
# Build script for USD Crate Examples

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Default values
BUILD_TYPE="standard"
USE_CMAKE=false
BUILD_DIR="build"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --monolithic)
            BUILD_TYPE="monolithic"
            shift
            ;;
        --cmake)
            USE_CMAKE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --monolithic    Build with monolithic USD library"
            echo "  --cmake         Use CMake instead of Make"
            echo "  --help          Show this help"
            echo ""
            echo "Examples:"
            echo "  $0                    # Standard build with Make"
            echo "  $0 --monolithic       # Monolithic build with Make"
            echo "  $0 --cmake            # Standard build with CMake"
            echo "  $0 --cmake --monolithic  # Monolithic build with CMake"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "USD Crate Examples Build Script"
echo "========================================"
echo "Build type: $BUILD_TYPE"
echo "Build system: $([ "$USE_CMAKE" = true ] && echo "CMake" || echo "Make")"
echo ""

if [ "$USE_CMAKE" = true ]; then
    # CMake build
    CMAKE_BUILD_DIR="build_cmake"
    [ "$BUILD_TYPE" = "monolithic" ] && CMAKE_BUILD_DIR="${CMAKE_BUILD_DIR}_monolithic"

    echo "Creating build directory: $CMAKE_BUILD_DIR"
    mkdir -p "$CMAKE_BUILD_DIR"
    cd "$CMAKE_BUILD_DIR"

    echo "Running CMake..."
    if [ "$BUILD_TYPE" = "monolithic" ]; then
        cmake .. -DUSE_MONOLITHIC_USD=ON
    else
        cmake .. -DUSE_MONOLITHIC_USD=OFF
    fi

    echo ""
    echo "Building..."
    cmake --build . -- -j$(nproc)

    echo ""
    echo "========================================"
    echo "Build complete!"
    echo "========================================"
    echo "Executables in: $CMAKE_BUILD_DIR/"
    ls -lh crate_*

else
    # Make build
    echo "Building with Make..."
    if [ "$BUILD_TYPE" = "monolithic" ]; then
        make MONOLITHIC=1 -j$(nproc)
    else
        make -j$(nproc)
    fi
fi

echo ""
echo "========================================"
echo "Success!"
echo "========================================"
