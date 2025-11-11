# TinyUSDZ Python ABI3 Binding Experiment

This is an experimental Python binding for TinyUSDZ using Python's stable ABI (Limited API) for Python 3.10+.

## Quick Start

See **[QUICKSTART.md](QUICKSTART.md)** for detailed setup instructions.

```bash
# Complete automated setup
./setup_env.sh

# Or use Makefile
make env           # Create environment with uv
source .venv/bin/activate
make build         # Build extension
make test          # Run tests
make examples      # Run examples
```

## Features

- **Stable ABI (ABI3)**: Binary compatible across Python 3.10+ versions
- **No Python Dev Headers Required**: Uses custom Python API headers
- **NumPy-Friendly**: Buffer protocol support for zero-copy array access
- **RAII Memory Management**: C++ side uses RAII, Python side uses ref counting
- **Minimal Dependencies**: Only requires C++14 compiler and NumPy

## Architecture

### Memory Management

- **C++ Side (TinyUSDZ)**: Uses RAII through the C API wrapper
  - Objects are created with `*_new()` functions
  - Objects are freed with `*_free()` functions
  - Automatic cleanup on scope exit

- **Python Side**: Uses reference counting
  - Objects are automatically deallocated when ref count reaches zero
  - Explicit `Py_INCREF`/`Py_DECREF` for lifetime management
  - No manual memory management needed from Python code

### Buffer Protocol

The `ValueArray` class implements Python's buffer protocol, making it compatible with NumPy and other array-processing libraries without data copying:

```python
import tinyusdz_abi3
import numpy as np

# Create a ValueArray (normally obtained from USD data)
array = stage.get_some_array_attribute()

# Zero-copy conversion to NumPy
np_array = np.asarray(array)

# The data is shared - no copying!
print(np_array.shape)
print(np_array.dtype)
```

## Building

### Method 1: Automated Setup with uv (Recommended)

```bash
# Complete setup: creates venv, installs deps, builds module
./setup_env.sh
```

### Method 2: Using Makefile

```bash
make env      # Create venv and install dependencies with uv
source .venv/bin/activate
make build    # Build extension module
make test     # Run tests
```

### Method 3: Using setup.py

```bash
# Install dependencies first
uv venv .venv
source .venv/bin/activate
uv pip install numpy setuptools wheel

# Build in-place
python setup.py build_ext --inplace

# Build wheel (creates a universal wheel for Python 3.10+)
python setup.py bdist_wheel
```

The resulting wheel will be named `tinyusdz_abi3-0.1.0-cp310-abi3-*.whl` and can be installed on any Python 3.10+ environment.

### Method 4: Using CMake

```bash
mkdir build && cd build
cmake ..
make
```

## Usage Examples

See `examples/` directory for complete examples:
- **example_basic.py** - Basic usage and object creation
- **example_numpy.py** - NumPy integration and buffer protocol
- **example_mesh_to_numpy.py** - Load mesh and convert to NumPy arrays

### Basic Stage Loading

```python
import tinyusdz_abi3 as tusd

# Load a USD file
stage = tusd.Stage.load_from_file("model.usd")

# Print stage contents
print(stage.to_string())
```

### Creating Values

```python
import tinyusdz_abi3 as tusd

# Create integer value
val_int = tusd.Value.from_int(42)
print(val_int.type)  # "int"
print(val_int.as_int())  # 42

# Create float value
val_float = tusd.Value.from_float(3.14)
print(val_float.type)  # "float"
print(val_float.as_float())  # 3.14
```

### Creating Prims

```python
import tinyusdz_abi3 as tusd

# Create a Mesh prim
mesh = tusd.Prim("Mesh")
print(mesh.type)  # "Mesh"

# Create an Xform prim
xform = tusd.Prim("Xform")
print(xform.type)  # "Xform"
```

### NumPy Integration (GeomMesh Example)

```python
import tinyusdz_abi3 as tusd
import numpy as np

# Load USD file with mesh
stage = tusd.Stage.load_from_file("mesh.usd")

# Get mesh prim (API to be implemented)
# mesh = stage.get_prim_at_path("/World/Mesh")
# positions = np.asarray(mesh.get_points())  # Zero-copy!
# indices = np.asarray(mesh.get_face_vertex_indices())
# normals = np.asarray(mesh.get_normals())

# For now, see example_mesh_to_numpy.py for demonstration
# Run: python examples/example_mesh_to_numpy.py mesh.usd

# The example shows:
# - Loading mesh geometry
# - Converting to NumPy arrays (zero-copy via buffer protocol)
# - Computing bounding boxes
# - Transform operations
# - Mesh statistics
```

Run the complete mesh example:

```bash
# With a USD file
python examples/example_mesh_to_numpy.py path/to/mesh.usd

# With synthetic data for demonstration
python examples/example_mesh_to_numpy.py
```

## Implementation Notes

### Custom Python Headers

This binding uses custom Python headers (`include/py_limited_api.h`) that define only the stable ABI subset. This means:

1. **No Python installation needed at build time** (on most platforms)
2. **Forward compatibility** - binary works with future Python versions
3. **Smaller dependency footprint** for embedded systems

### Value Types Supported

The binding supports all TinyUSDZ value types with optimized buffer protocol access:

- Scalars: `bool`, `int`, `uint`, `int64`, `uint64`, `float`, `double`, `half`
- Vectors: `int2/3/4`, `float2/3/4`, `double2/3/4`, `half2/3/4`
- Colors: `color3h/f/d`, `color4h/f/d`
- Geometry: `point3h/f/d`, `normal3h/f/d`, `vector3h/f/d`
- Texture: `texcoord2h/f/d`, `texcoord3h/f/d`
- Matrices: `matrix2d`, `matrix3d`, `matrix4d`
- Quaternions: `quath`, `quatf`, `quatd`

### Array Data Access

Arrays are exposed through Python's buffer protocol with appropriate format strings:

```python
# Example format strings
# "f"    - float32 scalar
# "fff"  - float32 vector3
# "d"    - float64 scalar
# "ddd"  - float64 vector3
```

This allows direct memory access from NumPy, memoryview, and other buffer-aware libraries.

## Testing

```bash
# Using Makefile
make test

# Or run directly
python tests/test_basic.py

# Run all examples
make examples

# Run mesh example
make mesh-example
```

## Advantages of ABI3

1. **Single Wheel for All Python 3.10+ versions**
   - No need to build separate wheels for 3.10, 3.11, 3.12, etc.
   - Reduces CI/CD complexity and storage requirements

2. **Future-Proof**
   - Binary compatible with Python versions not yet released
   - No need to rebuild when new Python versions come out

3. **Reduced Build Matrix**
   - Build once per platform (Windows/macOS/Linux)
   - No need to test against multiple Python versions

## Limitations

1. **Python 3.10+ Only**: Cannot support Python 3.9 or earlier
2. **Limited API Surface**: Only stable ABI functions available
3. **Slightly Larger Binary**: Some optimizations not available in stable ABI

## Performance Considerations

- **Zero-Copy Arrays**: Buffer protocol provides direct memory access
- **RAII on C++ Side**: Efficient memory management without Python GC overhead
- **Minimal Overhead**: Direct C API calls with thin Python wrapper

## Future Enhancements

- [ ] Complete Prim API (properties, relationships, metadata)
- [ ] Array attribute access with buffer protocol
- [ ] Stage traversal and path resolution
- [ ] Composition support (references, payloads, etc.)
- [ ] Type stubs (`.pyi` files) for IDE support
- [ ] Comprehensive test suite
- [ ] Benchmark comparisons with other USD Python bindings

## License

Apache 2.0 (same as TinyUSDZ)
