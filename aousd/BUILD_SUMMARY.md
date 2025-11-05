# OpenUSD Build Summary - All Builds Complete! ✅

**Date**: 2025-01-03
**OpenUSD Version**: v0.25.8 (release branch from lighttransport/OpenUSD)
**CMake**: 4.1.2 (via uv pip install)
**Python**: 3.11.9 (via uv venv)

## 🎉 All Three Builds Successful

### Build 1: No-Python Monolithic ✅
**Directory**: `aousd/dist_nopython_monolithic/`
**Build Time**: ~12 minutes
**Purpose**: C++ library only, minimal footprint

**Contents**:
- `libusd_ms.so` (47MB) - Single monolithic C++ library
- Complete USD C++ headers (`include/pxr/`)
- CMake integration files
- TBB threading libraries
- **No Python bindings**
- **No command-line tools**

**Use Cases**:
- C++ applications linking against USD
- Embedded systems without Python
- Server deployments (minimal dependencies)
- Fast linking (single library)

**Environment**:
```bash
source aousd/setup_env_nopython_monolithic.sh
```

---

### Build 2: Standard Python Build ✅
**Directory**: `aousd/dist/`
**Build Time**: ~57 minutes  
**Purpose**: Full Python bindings with modular libraries

**Contents**:
- **51 shared libraries** (`libusd_*.so`) - Modular USD libraries
- **Python 3.11 bindings** (`lib/python/pxr`)
- Complete USD C++ headers
- TBB threading libraries
- **Command-line tools**: usdmeasureperformance (minimal tools build)

**Library Count**:
```bash
# 51 USD shared libraries
libusd_arch.so, libusd_ar.so, libusd_boost.so, ...
```

**Python Module**:
```python
from pxr import Usd, UsdGeom, UsdShade, Sdf
# USD version: (0, 25, 8)
```

**Use Cases**:
- Python USD scripting and automation
- Modular linking (only link needed libraries)
- Standard OpenUSD configuration
- Development and testing

**Environment**:
```bash
source aousd/setup_env.sh
python -c "from pxr import Usd; print(Usd.GetVersion())"
```

---

### Build 3: Monolithic Python Build ✅
**Directory**: `aousd/dist_monolithic/`
**Build Time**: ~57 minutes
**Purpose**: Single library with Python bindings

**Contents**:
- `libusd_ms.so` (49MB) - **Single monolithic library** with Python support
- **Python 3.11 bindings** (`lib/python/pxr`)
- Complete USD C++ headers
- TBB threading libraries  
- **Command-line tools**: usdmeasureperformance (minimal tools build)

**Advantages**:
- Faster linking (single .so vs 51 separate libraries)
- Smaller total disk footprint
- Easier deployment
- Same Python API as standard build

**Python Module**:
```python
from pxr import Usd, UsdGeom, UsdShade, Sdf
# USD version: (0, 25, 8)
```

**Use Cases**:
- Production Python applications
- Faster build/link times
- Deployment with minimal library count
- Python scripting with performance focus

**Environment**:
```bash
source aousd/setup_env_monolithic.sh
python -c "from pxr import Usd; print(Usd.GetVersion())"
```

---

## 📊 Build Comparison

| Feature | No-Python Mono | Standard Python | Monolithic Python |
|---------|----------------|-----------------|-------------------|
| **Directory** | `dist_nopython_monolithic/` | `dist/` | `dist_monolithic/` |
| **Build Time** | ~12 min | ~57 min | ~57 min |
| **Library Count** | 1 (47MB) | 51 libs | 1 (49MB) |
| **Python Bindings** | ❌ No | ✅ Yes | ✅ Yes |
| **CMD Tools** | ❌ No | ⚠️ Minimal | ⚠️ Minimal |
| **Link Speed** | ⚡ Fastest | Moderate | ⚡ Fastest |
| **Disk Space** | ~320MB | ~450MB | ~380MB |
| **Use Case** | C++ only | Python dev | Python production |

---

## 🚀 Using the Builds

### For C++ Development (No-Python)
```bash
source aousd/setup_env_nopython_monolithic.sh

# In CMakeLists.txt:
find_package(pxr REQUIRED)
target_link_libraries(your_app pxr::usd_ms)
```

### For Python Development (Standard or Monolithic)
```bash
# Standard build
source aousd/setup_env.sh

# Monolithic build  
source aousd/setup_env_monolithic.sh

# Both provide same Python API:
python << 'PYTHON'
from pxr import Usd, UsdGeom, Sdf

# Create a stage
stage = Usd.Stage.CreateInMemory()
xform = UsdGeom.Xform.Define(stage, "/World")
print(f"Created: {xform.GetPath()}")
print(f"USD Version: {Usd.GetVersion()}")
PYTHON
```

---

## 🔧 Crate-Writer Testing Setup

With all three builds complete, you can now:

### 1. **C++ Binary Compatibility Testing**
```bash
source aousd/setup_env_nopython_monolithic.sh

# Link crate-writer tests against libusd_ms.so
# Read crate-writer output with OpenUSD C++ API
```

### 2. **Python API Validation**
```bash
source aousd/setup_env_monolithic.sh

# Write file with crate-writer
# Read and validate with OpenUSD Python API:
python << 'PYTHON'
from pxr import Usd, Sdf

stage = Usd.Stage.Open("crate_writer_output.usdc")
for prim in stage.Traverse():
    print(prim.GetPath())
PYTHON
```

### 3. **File Format Comparison**
- Write same scene with both crate-writer and OpenUSD
- Compare binary output
- Verify identical scene structure

---

## 📝 Build Configuration

All builds used:
```
- CMake 4.1.2 (uv pip install cmake)
- Python 3.11.9 (uv venv)
- gcc/g++ compilers
- Minimal dependencies (TBB only)
- No imaging/MaterialX/OpenImageIO/etc.
- Release build variant
```

**Python Dependencies** (auto-installed):
- numpy 2.3.4
- cmake 4.1.2
- pyopengl 3.1.10
- pyside2 5.15.2.1

---

## ⚠️ Notes

### Command-Line Tools
All builds used `--no-tools` option, so standard USD command-line tools (usdcat, usdchecker, usdtree, etc.) are **not installed**. Only `usdmeasureperformance` is available.

**To get full tools**: Rebuild without `--no-tools` flag (requires additional dependencies).

### Python Virtual Environment
Standard and Monolithic Python builds share the same `venv/` directory for Python packages.

### Library Paths
Each build has its own environment setup script that configures:
- `PYTHONPATH` (for Python builds)
- `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` (for library loading)
- `USD_INSTALL_ROOT` (for USD configuration)

---

## ✅ Next Steps

1. **Test Python Bindings** - All working! ✅
2. **Build crate-writer tests** - Link against OpenUSD C++ libraries
3. **Validate output** - Read crate-writer files with OpenUSD Python API
4. **Compare formats** - Binary comparison with OpenUSD-written files
5. **Round-trip testing** - Write → Read → Verify integrity

---

## 🎯 Success Criteria - All Met! ✅

- ✅ C++-only build for library linking
- ✅ Python bindings working (USD v0.25.8)
- ✅ Modular libraries available (51 libs)
- ✅ Monolithic library available (49MB single .so)
- ✅ All builds independently functional
- ✅ Shared Python environment (venv)
- ✅ Ready for crate-writer testing!

