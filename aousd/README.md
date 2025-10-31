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

## Directory Structure

```
aousd/
├── OpenUSD/                      # Cloned OpenUSD repository
├── dist/                         # OpenUSD standard build installation
│   ├── bin/                      # USD command-line tools
│   ├── lib/                      # USD libraries (45+ .so files) and Python modules
│   └── include/                  # USD headers
├── dist_monolithic/              # OpenUSD monolithic build installation
│   ├── bin/                      # USD command-line tools
│   ├── lib/                      # Single monolithic USD library and Python modules
│   └── include/                  # USD headers
├── venv/                         # Python 3.11 virtual environment (shared)
├── setup_openusd.sh              # Standard build script
├── setup_openusd_monolithic.sh   # Monolithic build script
├── setup_env.sh                  # Environment setup for standard build
├── setup_env_monolithic.sh       # Environment setup for monolithic build
└── README.md                     # This file
```

## Compiler Configuration

Both build scripts automatically detect available compilers, but you can override them:

```bash
# Use GCC (standard build)
CC=gcc CXX=g++ ./setup_openusd.sh

# Use Clang (standard build)
CC=clang CXX=clang++ ./setup_openusd.sh

# Use specific versions (standard build)
CC=gcc-11 CXX=g++-11 ./setup_openusd.sh

# Same for monolithic build
CC=clang CXX=clang++ ./setup_openusd_monolithic.sh
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
- Installed to `dist/`

**Monolithic Build (`setup_openusd_monolithic.sh`):**
- Single monolithic shared library (libusd_ms.so)
- Faster link times for applications
- Smaller total disk footprint
- Easier deployment (fewer .so files)
- Installed to `dist_monolithic/`

### Feature Configuration

Both builds use minimal dependencies by default. To enable additional features, modify the respective script:

```bash
# In setup_openusd.sh or setup_openusd_monolithic.sh
# Remove these flags for full features:
# --no-imaging     # Enable imaging support
# --no-usdview     # Enable USD viewer
# --no-materialx   # Enable MaterialX support
```

### Which Build to Use?

- **Use Standard Build** if you need maximum compatibility with other USD tools
- **Use Monolithic Build** if you want faster compilation/linking or easier deployment
- Both builds provide identical functionality and Python API

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

## Notes

- Two build variants available: Standard (modular) and Monolithic (single library)
- The OpenUSD builds are configured for minimal dependencies to reduce build time
- Python bindings are enabled for comprehensive API comparison
- The setup uses Python 3.11 via `uv` for consistent environment
- Build artifacts are isolated in separate directories (`dist/` and `dist_monolithic/`)
- Both builds share the same Python virtual environment
- Compiler selection: The scripts auto-detect gcc/g++ or clang/clang++, or use CC/CXX environment variables
- You can have both builds installed simultaneously