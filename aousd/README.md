# OpenUSD Environment for TinyUSDZ Comparison

This directory contains scripts and tools for setting up OpenUSD to compare its behavior and output with TinyUSDZ.

## Quick Start

### Standard Build (Multiple Shared Libraries)

1. **Initial Setup** (one-time only):
   ```bash
   cd aousd
   ./setup_openusd.sh
   ```

   Or with specific compilers:
   ```bash
   CC=clang CXX=clang++ ./setup_openusd.sh
   ```

   This will:
   - Clone OpenUSD repository (release branch) from https://github.com/lighttransport/OpenUSD
   - Set up Python 3.11 virtual environment using `uv`
   - Configure C/C++ compilers (auto-detects or uses CC/CXX environment variables)
   - Build OpenUSD with Python bindings and minimal dependencies
   - Install to `aousd/dist`

2. **Activate Environment** (every new terminal session):
   ```bash
   source aousd/setup_env.sh
   ```

### Monolithic Build (Single Shared Library)

For applications that benefit from a single monolithic USD library:

1. **Initial Setup** (one-time only):
   ```bash
   cd aousd
   ./setup_openusd_monolithic.sh
   ```

   Or with specific compilers:
   ```bash
   CC=clang CXX=clang++ ./setup_openusd_monolithic.sh
   ```

   This will:
   - Build OpenUSD as a single monolithic shared library (`-DPXR_BUILD_MONOLITHIC=ON`)
   - Install to `aousd/dist_monolithic`
   - Use the same Python virtual environment

2. **Activate Environment** (every new terminal session):
   ```bash
   source aousd/setup_env_monolithic.sh
   ```

**Monolithic vs Standard Build:**
- **Monolithic**: Single `libusd_ms.so` library, faster linking, smaller total size
- **Standard**: Multiple libraries (45+ .so files), more modular, standard OpenUSD configuration

### No-Python Build (C++-Only)

For C++-only applications without Python dependencies:

#### Standard No-Python Build (Multiple Libraries)

1. **Initial Setup** (one-time only):
   ```bash
   cd aousd
   ./setup_openusd_nopython.sh
   ```

   Or with specific compilers:
   ```bash
   CC=clang CXX=clang++ ./setup_openusd_nopython.sh
   ```

   This will:
   - Build OpenUSD without Python bindings (`--no-python`)
   - Install to `aousd/dist_nopython`
   - 43 USD libraries (modular linking)
   - No Python virtual environment required

2. **Activate Environment** (every new terminal session):
   ```bash
   source aousd/setup_env_nopython.sh
   ```

#### Monolithic No-Python Build (Single Library)

1. **Initial Setup** (one-time only):
   ```bash
   cd aousd
   ./setup_openusd_nopython_monolithic.sh
   ```

   Or with specific compilers:
   ```bash
   CC=clang CXX=clang++ ./setup_openusd_nopython_monolithic.sh
   ```

   This will:
   - Build OpenUSD as single monolithic library without Python
   - Install to `aousd/dist_nopython_monolithic`
   - Single `libusd_ms.so` library (fastest linking)
   - No Python virtual environment required

2. **Activate Environment** (every new terminal session):
   ```bash
   source aousd/setup_env_nopython_monolithic.sh
   ```

**Benefits of No-Python Builds:**
- Smaller binary size (no Python interpreter embedded)
- Faster build time (no Python bindings to compile)
- Minimal runtime dependencies (C++ stdlib + TBB only)
- Ideal for embedded systems or server deployments
- Perfect for C++ applications that don't need Python API
- **Monolithic variant**: Single library for even faster linking

## Directory Structure

```
aousd/
├── OpenUSD/                            # Cloned OpenUSD repository
├── dist/                               # OpenUSD standard build (with Python)
│   ├── bin/                            # USD command-line tools
│   ├── lib/                            # USD libraries (45+ .so files) and Python modules
│   └── include/                        # USD headers
├── dist_monolithic/                    # OpenUSD monolithic build (with Python)
│   ├── bin/                            # USD command-line tools
│   ├── lib/                            # Single monolithic USD library and Python modules
│   └── include/                        # USD headers
├── dist_nopython/                      # OpenUSD no-python build (C++-only)
│   ├── lib/                            # 43 USD C++ libraries (no Python modules)
│   └── include/                        # USD headers
├── dist_nopython_monolithic/           # OpenUSD no-python monolithic (C++-only)
│   ├── lib/                            # Single libusd_ms.so (no Python modules)
│   └── include/                        # USD headers
├── venv/                               # Python 3.11 virtual environment (shared by Python builds)
├── setup_openusd.sh                    # Standard build script
├── setup_openusd_monolithic.sh         # Monolithic build script
├── setup_openusd_nopython.sh           # No-Python standard build script
├── setup_openusd_nopython_monolithic.sh # No-Python monolithic build script
├── setup_env.sh                        # Environment setup for standard build
├── setup_env_monolithic.sh             # Environment setup for monolithic build
├── setup_env_nopython.sh               # Environment setup for no-python build
├── setup_env_nopython_monolithic.sh    # Environment setup for no-python monolithic build
└── README.md                           # This file
```

## Compiler Configuration

All build scripts automatically detect available compilers, but you can override them:

```bash
# Use GCC
CC=gcc CXX=g++ ./setup_openusd.sh
CC=gcc CXX=g++ ./setup_openusd_monolithic.sh
CC=gcc CXX=g++ ./setup_openusd_nopython.sh
CC=gcc CXX=g++ ./setup_openusd_nopython_monolithic.sh

# Use Clang
CC=clang CXX=clang++ ./setup_openusd.sh
CC=clang CXX=clang++ ./setup_openusd_monolithic.sh
CC=clang CXX=clang++ ./setup_openusd_nopython.sh
CC=clang CXX=clang++ ./setup_openusd_nopython_monolithic.sh

# Use specific versions
CC=gcc-11 CXX=g++-11 ./setup_openusd.sh
```

## Available Tools After Setup

Once the environment is activated, you can use:

### Command-line Tools
- `usdcat` - Display USD files in text format
- `usddiff` - Compare two USD files
- `usdtree` - Display USD scene hierarchy
- `usdchecker` - Validate USD files
- `usdzip` - Create USDZ archives

### Python API
```python
from pxr import Usd, UsdGeom, UsdShade

# Load a USD file
stage = Usd.Stage.Open("../models/suzanne.usda")

# Traverse the stage
for prim in stage.Traverse():
    print(prim.GetPath())
```

## Comparison Examples

### 1. Compare File Parsing

**TinyUSDZ:**
```bash
# From tinyusdz root
./build/tusdcat models/suzanne.usda > tinyusdz_output.txt
```

**OpenUSD:**
```bash
# After sourcing setup_env.sh
usdcat ../models/suzanne.usda > openusd_output.txt
```

**Compare outputs:**
```bash
diff tinyusdz_output.txt openusd_output.txt
```

### 2. Validate USD Files

**OpenUSD validation:**
```bash
usdchecker ../models/suzanne.usda
```

### 3. Compare Scene Hierarchy

**TinyUSDZ:**
```bash
# Use tusdview or custom tool to display hierarchy
./build/tusdview models/suzanne.usda
```

**OpenUSD:**
```bash
usdtree ../models/suzanne.usda
```

### 4. Python Script Comparison

Create a test script `compare_usd.py`:

```python
#!/usr/bin/env python

import sys
import json

# For OpenUSD (when environment is activated)
try:
    from pxr import Usd, UsdGeom

    def analyze_with_openusd(filepath):
        stage = Usd.Stage.Open(filepath)
        result = {
            "prim_count": len(list(stage.Traverse())),
            "root_layer": stage.GetRootLayer().identifier,
            "up_axis": UsdGeom.GetStageUpAxis(stage),
            "meters_per_unit": UsdGeom.GetStageMetersPerUnit(stage)
        }
        return result

    if len(sys.argv) > 1:
        result = analyze_with_openusd(sys.argv[1])
        print("OpenUSD Analysis:")
        print(json.dumps(result, indent=2))

except ImportError:
    print("OpenUSD not available")
```

### 5. Compare USDZ Creation

**TinyUSDZ:**
```bash
# Use TinyUSDZ's USDZ creation functionality
# (implementation depends on TinyUSDZ API)
```

**OpenUSD:**
```bash
usdzip output.usdz -r models/suzanne.usda
```

## Build Options

### Build Type Selection

**Standard Build (`setup_openusd.sh`):**
- Multiple shared libraries (libusd_arch.so, libusd_sdf.so, libusd_usd.so, etc.)
- Standard OpenUSD configuration used by most applications
- Modular library structure allows selective linking
- Python bindings included
- Installed to `dist/`

**Monolithic Build (`setup_openusd_monolithic.sh`):**
- Single monolithic shared library (libusd_ms.so)
- Faster link times for applications
- Smaller total disk footprint
- Easier deployment (fewer .so files)
- Python bindings included
- Installed to `dist_monolithic/`

**No-Python Standard Build (`setup_openusd_nopython.sh`):**
- 43 USD shared libraries (C++ only)
- No Python interpreter or bindings
- Minimal runtime dependencies (C++ stdlib + TBB)
- Faster build time (~15-18 min)
- Smallest binary size (~320 MB)
- Ideal for C++-only applications with modular linking
- Installed to `dist_nopython/`

**No-Python Monolithic Build (`setup_openusd_nopython_monolithic.sh`):**
- Single `libusd_ms.so` shared library (C++ only)
- No Python interpreter or bindings
- Minimal runtime dependencies (C++ stdlib + TBB)
- Faster build time (~15-18 min)
- Small binary size (~417 MB)
- Fastest linking for C++-only applications
- Ideal for production deployments with minimal footprint
- Installed to `dist_nopython_monolithic/`

### Feature Configuration

All builds use minimal dependencies by default. To enable additional features, modify the respective script:

```bash
# In setup_openusd.sh, setup_openusd_monolithic.sh, or setup_openusd_nopython.sh
# Remove these flags for full features:
# --no-imaging     # Enable imaging support
# --no-usdview     # Enable USD viewer
# --no-materialx   # Enable MaterialX support
```

### Which Build to Use?

- **Use Standard Build** if you need maximum compatibility with other USD tools and Python API
- **Use Monolithic Build** if you want faster compilation/linking or easier deployment with Python
- **Use No-Python Standard Build** if you only need C++ libraries with modular linking
- **Use No-Python Monolithic Build** if you want C++-only with fastest linking and minimal deployment
- All builds provide identical C++ API functionality

## Troubleshooting

### Build Fails
- Ensure you have CMake 3.12+ installed
- Check for required system dependencies:
  ```bash
  # Ubuntu/Debian
  sudo apt-get install build-essential cmake python3-dev

  # macOS
  brew install cmake
  ```

### Python Import Errors
- Verify environment is activated: `source aousd/setup_env.sh`
- Check Python version: `python --version` (should be 3.11.x)
- Verify PYTHONPATH: `echo $PYTHONPATH`

### Missing uv Command
The script will automatically install `uv` if not present. Alternatively:
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

## Useful Comparison Scripts

Create `aousd/compare_tools.sh`:

```bash
#!/bin/bash

USD_FILE="${1:-../models/suzanne.usda}"

echo "Comparing USD file: $USD_FILE"
echo "================================"

# Ensure environment is set up
source "$(dirname "$0")/setup_env.sh"

# Create comparison directory
mkdir -p comparisons
cd comparisons

# OpenUSD outputs
echo "Generating OpenUSD outputs..."
usdcat "$USD_FILE" > openusd_cat.txt
usdtree "$USD_FILE" > openusd_tree.txt
usdchecker "$USD_FILE" > openusd_check.txt 2>&1

# TinyUSDZ outputs (adjust paths as needed)
echo "Generating TinyUSDZ outputs..."
../../build/tusdcat "$USD_FILE" > tinyusdz_cat.txt

echo "Outputs saved in comparisons/"
echo "Use 'diff' or 'vimdiff' to compare files"
```

## Additional Documentation

- **[Build Comparison Guide](README_BUILD_COMPARISON.md)** - Detailed comparison of all three build variants with use cases, performance characteristics, and recommendations

## Notes

- **Four build variants available:**
  1. **Standard** (dist/): 45+ libraries with Python bindings + command-line tools
  2. **Monolithic** (dist_monolithic/): Single library with Python bindings + command-line tools
  3. **No-Python Standard** (dist_nopython/): 43 C++ libraries without Python
  4. **No-Python Monolithic** (dist_nopython_monolithic/): Single C++ library without Python
- The OpenUSD builds are configured for minimal dependencies to reduce build time
- Python builds use Python 3.11 via `uv` for consistent environment
- Build artifacts are isolated in separate directories
- Python builds (1 & 2) share the same Python virtual environment
- No-Python builds (3 & 4) have no Python dependencies - only C++ stdlib + TBB
- Compiler selection: The scripts auto-detect gcc/g++ or clang/clang++, or use CC/CXX environment variables
- You can have all four builds installed simultaneously
- Command-line tools (usdcat, etc.) are only available in Python builds (1 & 2)
- No-Python builds are library-only for C++ development
- See [README_BUILD_COMPARISON.md](README_BUILD_COMPARISON.md) for help choosing the right build for your needs