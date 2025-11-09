# C++ Build Examples for OpenUSD + TinyUSDZ

This directory contains two build system examples for creating C++ applications that use both OpenUSD and TinyUSDZ libraries for comparison and testing.

## Prerequisites

Before building, ensure you have:

1. **OpenUSD built and installed** in `aousd/dist/`:
   ```bash
   cd aousd
   ./setup_openusd.sh  # If not already built
   ```

2. **TinyUSDZ built** in the main `build/` directory:
   ```bash
   cd ../..  # Go to TinyUSDZ root
   mkdir build && cd build
   cmake .. -DTINYUSDZ_BUILD_STATIC_LIB=ON
   make tinyusdz_static
   ```

3. **Required compilers**: clang-20 and clang++-20 (or modify the build files for your compiler)

## Directory Structure

```
aousd/
├── dist/                 # OpenUSD installation
├── cpp_makefile/        # Makefile-based project
│   ├── Makefile
│   └── main.cpp
└── cpp_cmake/           # CMake-based project
    ├── CMakeLists.txt
    ├── build.sh
    └── main.cpp
```

## Option 1: Makefile Build

Simple, direct Makefile approach.

### Building

```bash
cd cpp_makefile
make
```

### Running

```bash
make run
# or
LD_LIBRARY_PATH=../dist/lib ./usd_comparison ../../models/suzanne.usda
```

### Makefile Commands

- `make` - Build the application
- `make clean` - Remove build artifacts
- `make run` - Build and run with proper library paths
- `make debug` - Print build variables
- `make help` - Show available commands

### Customization

Edit the Makefile to adjust:
- `CXX` - C++ compiler (default: clang++-20)
- `OPENUSD_ROOT` - Path to OpenUSD installation
- `TINYUSDZ_BUILD` - Path to TinyUSDZ build directory

## Option 2: CMake Build

More portable and feature-rich build system.

### Building

```bash
cd cpp_cmake
chmod +x build.sh
./build.sh
```

Or manually:

```bash
cd cpp_cmake
mkdir build && cd build
CC=clang-20 CXX=clang++-20 cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENUSD_ROOT=../../dist \
    -DTINYUSDZ_ROOT=../../.. \
    -DTINYUSDZ_BUILD=../../../build
make -j
```

### Running

```bash
cd build
LD_LIBRARY_PATH=../../dist/lib ./usd_comparison
# or
make run
```

### CMake Options

- `-DCMAKE_BUILD_TYPE=[Debug|Release]` - Build configuration
- `-DOPENUSD_ROOT=path` - OpenUSD installation path
- `-DTINYUSDZ_ROOT=path` - TinyUSDZ source path
- `-DTINYUSDZ_BUILD=path` - TinyUSDZ build directory

## The Example Application

Both build systems compile the same `main.cpp` that:

1. **Loads a USD file** using both libraries
2. **Compares metadata**: Up axis, meters per unit
3. **Lists prims** and their types
4. **Counts meshes** and other elements
5. **Tests Tydra conversion** (TinyUSDZ's render scene converter)

### Sample Output

```
USD Comparison Tool (OpenUSD + TinyUSDZ)
=========================================

=== OpenUSD Analysis ===
Successfully opened: ../../models/suzanne.usda
Root layer: ../../models/suzanne.usda
Up axis: Z
Meters per unit: 1

Prims in stage:
  /Suzanne [Xform]
  /Suzanne/Suzanne [Mesh] - 500 faces
Total prims: 2

=== TinyUSDZ Analysis ===
Successfully loaded: ../../models/suzanne.usda
Up axis: Z
Meters per unit: 1

Root prims:
  /Suzanne [Xform]
    Has 1 children
Estimated prim count: 2

=========================================
Comparison complete!
```

## Library Dependencies

### OpenUSD Libraries Used
- Core: `usd`, `sdf`, `pcp`
- Geometry: `usdGeom`
- Shading: `usdShade`
- Utilities: `tf`, `vt`, `arch`, `trace`

### TinyUSDZ Libraries Used
- `tinyusdz_static` - Main static library
- Includes Tydra for render scene conversion

### System Libraries
- `tbb` - Intel Threading Building Blocks
- `pthread` - POSIX threads
- `dl` - Dynamic loading
- `m` - Math library

## Troubleshooting

### Library Not Found

If you get library loading errors:
```bash
export LD_LIBRARY_PATH=/path/to/aousd/dist/lib:$LD_LIBRARY_PATH
```

### Undefined Symbols

Ensure both OpenUSD and TinyUSDZ are built with the same compiler:
```bash
CC=clang-20 CXX=clang++-20 ./setup_openusd.sh
```

### TinyUSDZ Static Library Missing

Build it in TinyUSDZ root:
```bash
cd ../..
mkdir -p build && cd build
cmake .. -DTINYUSDZ_BUILD_STATIC_LIB=ON
make tinyusdz_static
```

## Extending the Examples

To add more comparison features:

1. **Add more OpenUSD analysis**:
   ```cpp
   // Check for specific prim types
   if (prim.IsA<UsdGeomCamera>()) {
       // Handle camera
   }
   ```

2. **Add TinyUSDZ features**:
   ```cpp
   // Access animation
   if (stage.has_timesamples()) {
       // Process animations
   }
   ```

3. **Compare specific attributes**:
   ```cpp
   // Compare transform matrices, materials, etc.
   ```

## Notes

- Both build systems use clang-20 by default to match the OpenUSD build
- The example focuses on basic scene structure comparison
- Extend the code for specific USD features you want to compare
- The Makefile approach is simpler but less portable
- The CMake approach is recommended for larger projects