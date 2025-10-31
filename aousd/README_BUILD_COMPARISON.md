# OpenUSD Build Variants Comparison

This document helps you choose the right OpenUSD build variant for your use case.

## Quick Decision Guide

```
Do you need Python API?
│
├─ YES → Do you need many .so files or single library?
│   ├─ Single library → Use MONOLITHIC build
│   └─ Multiple libraries → Use STANDARD build
│
└─ NO → Use NO-PYTHON build
```

## Detailed Comparison Table

| Feature | Standard | Monolithic | No-Python |
|---------|----------|------------|-----------|
| **Python Bindings** | ✅ Yes | ✅ Yes | ❌ No |
| **Library Structure** | Multiple .so files (45+) | Single libusd_ms.so | Multiple .so files |
| **Build Time** | ~15-30 min | ~15-30 min | ~10-20 min (fastest) |
| **Binary Size** | Largest | Medium | Smallest |
| **Link Time** | Slow | Fast | Medium |
| **Runtime Dependencies** | Python 3.11 + libs | Python 3.11 + libs | C++ stdlib only |
| **Installation Dir** | `dist/` | `dist_monolithic/` | `dist_nopython/` |
| **Best For** | Development & scripting | Production apps with Python | C++ apps, embedded systems |

## Use Cases

### Standard Build (`setup_openusd.sh`)

**Best for:**
- Development and prototyping
- Python scripting and automation
- Maximum compatibility with OpenUSD ecosystem
- Applications that need to selectively link USD modules

**Example scenarios:**
```bash
# Python scripting for USD file manipulation
python process_usd.py --input scene.usd --output modified.usd

# Pipeline tools with Python bindings
from pxr import Usd, UsdGeom
stage = Usd.Stage.Open("model.usd")
```

### Monolithic Build (`setup_openusd_monolithic.sh`)

**Best for:**
- Production applications with Python API
- Faster linking during development
- Simplified deployment (fewer .so files to manage)
- Applications that use most USD modules

**Example scenarios:**
```bash
# Production pipeline tool with embedded Python
./my_app --python-script process.py --input scene.usd

# DCC plugins with Python support
# (Maya, Blender plugins that use both C++ and Python USD)
```

**Advantages over Standard:**
- Single `libusd_ms.so` instead of 45+ separate .so files
- Faster link times (link once vs. many libraries)
- Easier to distribute (fewer files to package)
- Reduced startup overhead (load one library vs. many)

### No-Python Build (`setup_openusd_nopython.sh`)

**Best for:**
- Pure C++ applications
- Embedded systems with limited resources
- Server-side processing without Python
- Minimal deployment footprint
- CI/CD environments where Python is not needed

**Example scenarios:**
```cpp
// Pure C++ application for USD processing
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

int main() {
    auto stage = pxr::UsdStage::Open("model.usd");
    // Process USD data in C++
    return 0;
}
```

**Advantages:**
- No Python runtime required
- Smaller binary size (no Python interpreter)
- Faster build times (no Python bindings to compile)
- Lower memory footprint
- Simplified deployment (no Python virtual environment)

## Build Time & Size Estimates

Based on typical Linux x86_64 system:

| Build Type | Build Time | Install Size | Runtime Deps |
|------------|------------|--------------|--------------|
| Standard | 20-25 min | ~500 MB | Python 3.11 + venv (~200 MB) |
| Monolithic | 20-25 min | ~450 MB | Python 3.11 + venv (~200 MB) |
| No-Python | 15-18 min | ~300 MB | None (system libs only) |

*Note: Times are approximate and depend on CPU cores and compiler optimization level.*

## Command-Line Tools Comparison

All three builds include the same USD command-line tools:

| Tool | Standard | Monolithic | No-Python |
|------|----------|------------|-----------|
| `usdcat` | ✅ | ✅ | ✅ |
| `usddiff` | ✅ | ✅ | ✅ |
| `usdtree` | ✅ | ✅ | ✅ |
| `usdchecker` | ✅ | ✅ | ✅ |
| `usdzip` | ✅ | ✅ | ✅ |
| Python API | ✅ | ✅ | ❌ |

## C++ Development Considerations

### Linking Your Application

**Standard or No-Python build:**
```cmake
# CMakeLists.txt
find_package(pxr REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp
    usd
    usdGeom
    sdf
    # ... other USD modules as needed
)
```

**Monolithic build:**
```cmake
# CMakeLists.txt
find_package(pxr REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp
    usd_ms  # Single monolithic library
)
```

### When to Choose Each for C++ Development

**Standard/No-Python:**
- Fine-grained control over linked modules
- Smaller final binary if you only use a few USD modules
- Better for libraries that expose USD as optional dependency

**Monolithic:**
- Simplest linking (just one library)
- Better if you use many USD modules
- Faster link times during development iteration

## Migration Between Builds

You can have all three builds installed simultaneously. They install to different directories and don't conflict:

```bash
# Install all three
./setup_openusd.sh              # → dist/
./setup_openusd_monolithic.sh   # → dist_monolithic/
./setup_openusd_nopython.sh     # → dist_nopython/

# Switch between them by sourcing different env scripts
source setup_env.sh              # Use standard build
source setup_env_monolithic.sh   # Use monolithic build
source setup_env_nopython.sh     # Use no-python build
```

## Performance Considerations

### Startup Time
- **Monolithic**: Fastest (load one .so)
- **No-Python**: Fast (no Python interpreter startup)
- **Standard**: Slower (load many .so files)

### Link Time (Development)
- **Monolithic**: Fastest (link one library)
- **No-Python**: Medium
- **Standard**: Slowest (link many libraries)

### Runtime Performance
All three have **identical runtime performance** for C++ code. The Python builds have additional overhead only when using Python features.

## Recommendations

### For TinyUSDZ Comparison Testing
**Use Standard or No-Python build** - Most compatible with TinyUSDZ's C++ focus.

### For Pipeline Development
**Use Standard build** - Python scripting is essential for pipelines.

### For Production Deployment
- **With Python needs**: Monolithic build
- **C++ only**: No-Python build

### For CI/CD Testing
**Use No-Python build** - Fastest build time and minimal dependencies.

### For Cross-Platform Development
**Use Standard build** - Most widely tested configuration.

## Building Multiple Variants

You can build all three variants efficiently:

```bash
# Build all three in sequence (each takes 15-25 min)
cd aousd

# 1. Standard build (creates venv)
./setup_openusd.sh

# 2. Monolithic build (reuses venv)
./setup_openusd_monolithic.sh

# 3. No-Python build (no venv needed)
./setup_openusd_nopython.sh

# Total time: ~60-75 minutes
# Total disk space: ~1.2 GB + ~200 MB for venv
```

The OpenUSD source repository is shared between all builds, so cloning happens only once.
