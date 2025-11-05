# OpenUSD Quick Start Guide

## 🚀 Quick Reference

### Three Builds Available

```bash
cd /mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/aousd/

# 1. No-Python Monolithic (C++ only)
source setup_env_nopython_monolithic.sh

# 2. Standard Python (51 libraries)  
source setup_env.sh

# 3. Monolithic Python (1 library)
source setup_env_monolithic.sh
```

---

## 📖 Usage Examples

### Python API (Standard or Monolithic)

```bash
# Activate environment
source aousd/setup_env_monolithic.sh

# Test Python bindings
python << 'PYTHON'
from pxr import Usd, UsdGeom, Sdf

# Create a simple stage
stage = Usd.Stage.CreateInMemory("example.usdc")

# Add a transform
xform = UsdGeom.Xform.Define(stage, "/World")
sphere = UsdGeom.Sphere.Define(stage, "/World/Sphere")
sphere.GetRadiusAttr().Set(1.0)

# Export to file
stage.GetRootLayer().Export("example.usdc")
print(f"Created example.usdc with USD {Usd.GetVersion()}")
PYTHON
```

### Read Crate-Writer Output

```bash
source aousd/setup_env_monolithic.sh

python << 'PYTHON'
from pxr import Usd, Sdf

# Open file written by crate-writer
stage = Usd.Stage.Open("crate_writer_output.usdc")

# Traverse and print
for prim in stage.Traverse():
    print(f"Prim: {prim.GetPath()} (type: {prim.GetTypeName()})")
    
# Check layer info
layer = stage.GetRootLayer()
print(f"\nLayer identifier: {layer.identifier}")
print(f"File format: {layer.GetFileFormat().formatId}")
PYTHON
```

### C++ Linking (No-Python Build)

```bash
source aousd/setup_env_nopython_monolithic.sh

# CMakeLists.txt example:
cat > CMakeLists.txt << 'CMAKE'
cmake_minimum_required(VERSION 3.26)
project(MyUSDApp)

set(CMAKE_CXX_STANDARD 17)

# Find USD
find_package(pxr REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp pxr::usd_ms)
CMAKE
```

---

## 📊 Build Comparison

| Build Type | Environment Script | Python | Libraries | Size |
|-----------|-------------------|--------|-----------|------|
| No-Python Mono | `setup_env_nopython_monolithic.sh` | ❌ | 1 (47MB) | ~320MB |
| Standard Python | `setup_env.sh` | ✅ | 51 | ~450MB |
| Monolithic Python | `setup_env_monolithic.sh` | ✅ | 1 (49MB) | ~380MB |

---

## 🎯 Choose Your Build

**For Crate-Writer Testing:**
- **Python validation**: Use `setup_env_monolithic.sh` (Python API + fastest linking)
- **C++ integration**: Use `setup_env_nopython_monolithic.sh` (C++ only)
- **Development**: Use `setup_env.sh` (modular libraries)

**Quick Test:**
```bash
# Test Python build
source aousd/setup_env_monolithic.sh
python -c "from pxr import Usd; print(f'USD {Usd.GetVersion()} OK!')"
```

---

## 📁 Directory Structure

```
aousd/
├── OpenUSD/                      # Source repository
├── venv/                         # Shared Python virtual environment
├── .venv/                        # Build tools (CMake 4.1.2)
│
├── dist_nopython_monolithic/     # C++ only build
│   ├── lib/libusd_ms.so (47MB)
│   └── include/pxr/
│
├── dist/                         # Standard Python build
│   ├── lib/libusd_*.so (51 libs)
│   ├── lib/python/pxr/
│   └── include/pxr/
│
├── dist_monolithic/              # Monolithic Python build
│   ├── lib/libusd_ms.so (49MB)
│   ├── lib/python/pxr/
│   └── include/pxr/
│
├── BUILD_SUMMARY.md              # Comprehensive build documentation
├── BUILD_SUCCESS.md              # No-Python build details
└── QUICK_START.md                # This file
```

---

## ✅ Verification

All builds verified working:
```bash
# No-Python C++ library
ls -lh aousd/dist_nopython_monolithic/lib/libusd_ms.so
# -rw-r--r-- 1 user user 47M

# Standard Python (51 libraries)
ls aousd/dist/lib/libusd_*.so | wc -l
# 51

# Monolithic Python  
ls -lh aousd/dist_monolithic/lib/libusd_ms.so
# -rw-r--r-- 1 user user 49M

# Python bindings
source aousd/setup_env.sh
python -c "from pxr import Usd; print(Usd.GetVersion())"
# (0, 25, 8)
```

---

## 🔗 Next Steps

1. **Test crate-writer output** with Python API
2. **Link C++ tests** against libusd_ms.so
3. **Compare file formats** between crate-writer and OpenUSD
4. **Round-trip testing** (write → read → validate)

All builds are ready for crate-writer testing! 🎉
