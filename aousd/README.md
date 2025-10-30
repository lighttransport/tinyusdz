# OpenUSD Environment for TinyUSDZ Comparison

This directory contains scripts and tools for setting up OpenUSD to compare its behavior and output with TinyUSDZ.

## Quick Start

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

## Directory Structure

```
aousd/
├── OpenUSD/          # Cloned OpenUSD repository
├── dist/             # OpenUSD installation
│   ├── bin/          # USD command-line tools
│   ├── lib/          # USD libraries and Python modules
│   └── include/      # USD headers
├── venv/             # Python 3.11 virtual environment
├── setup_openusd.sh  # Build and installation script
├── setup_env.sh      # Environment setup script
└── README.md         # This file
```

## Compiler Configuration

The build script automatically detects available compilers, but you can override them:

```bash
# Use GCC
CC=gcc CXX=g++ ./setup_openusd.sh

# Use Clang
CC=clang CXX=clang++ ./setup_openusd.sh

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

The current setup uses minimal dependencies. To enable additional features, modify `setup_openusd.sh`:

```bash
# Remove these flags for full features:
# --no-imaging     # Enable imaging support
# --no-usdview     # Enable USD viewer
# --no-materialx   # Enable MaterialX support
```

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

- The OpenUSD build is configured for minimal dependencies to reduce build time
- Python bindings are enabled for comprehensive API comparison
- The setup uses Python 3.11 via `uv` for consistent environment
- Build artifacts are isolated in `aousd/` directory for clean separation
- Compiler selection: The script auto-detects gcc/g++ or clang/clang++, or uses CC/CXX environment variables