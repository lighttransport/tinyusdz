# TinyUSDZ ABI3 Binding - Quick Reference Card

## Setup Commands

```bash
# One-line setup (recommended)
./setup_env.sh && source .venv/bin/activate

# Or step by step
uv venv .venv
source .venv/bin/activate
uv pip install numpy
python setup.py build_ext --inplace
```

## Makefile Targets

```bash
make env           # Create venv with uv and install deps
make build         # Build extension module
make test          # Run tests
make examples      # Run all examples
make mesh-example  # Run mesh example
make clean         # Remove build artifacts
make distclean     # Remove everything including venv
make help          # Show all targets
```

## Running Examples

```bash
# Basic example
python examples/example_basic.py

# NumPy integration
python examples/example_numpy.py

# Mesh to NumPy
python examples/example_mesh_to_numpy.py [usd_file]
```

## Python API

```python
import tinyusdz_abi3 as tusd
import numpy as np

# Load USD file
stage = tusd.Stage.load_from_file("scene.usd")
print(stage.to_string())

# Create objects
prim = tusd.Prim("Mesh")
val = tusd.Value.from_int(42)

# Detect format
fmt = tusd.detect_format("file.usda")  # Returns "USDA"

# Future: Mesh data access (to be implemented)
# mesh = stage.get_prim_at_path("/World/Mesh")
# positions = np.asarray(mesh.get_points())
# indices = np.asarray(mesh.get_face_vertex_indices())
```

## Installing uv

```bash
# Linux/macOS
curl -LsSf https://astral.sh/uv/install.sh | sh

# With pip
pip install uv

# With cargo
cargo install uv
```

## Building Wheels

```bash
# Build wheel
python setup.py bdist_wheel

# Wheel is in dist/ directory
# Install with:
pip install dist/tinyusdz_abi3-*.whl
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `uv: command not found` | Install uv (see above) |
| Can't import module | Run `python setup.py build_ext --inplace` |
| NumPy missing | Run `uv pip install numpy` |
| Build errors | Install C++ compiler (gcc/clang/MSVC) |

## File Overview

| File | Purpose |
|------|---------|
| `setup_env.sh` | Complete automated setup |
| `Makefile` | Build automation |
| `setup.py` | Python package build |
| `CMakeLists.txt` | CMake build |
| `include/py_limited_api.h` | Custom Python headers |
| `src/tinyusdz_abi3.c` | Main binding code |
| `src/tinyusdz_mesh_api.c` | Mesh API (placeholder) |
| `examples/example_mesh_to_numpy.py` | Mesh demo |
| `tests/test_basic.py` | Unit tests |

## Key Features

✓ **ABI3** - One binary for Python 3.10+
✓ **Zero-copy** - Buffer protocol for NumPy
✓ **No deps** - No python3-dev needed at build time
✓ **RAII** - Automatic C++ memory management
✓ **NumPy-ready** - Native array support

## Documentation

- `README.md` - Full documentation
- `QUICKSTART.md` - Setup guide
- `DESIGN.md` - Technical architecture
- `SUMMARY.md` - Project overview
- `REFERENCE.md` - This file

## Common Workflows

### Development

```bash
# Setup once
./setup_env.sh
source .venv/bin/activate

# Edit code...

# Rebuild
make build

# Test
make test
```

### Using with NumPy

```python
import tinyusdz_abi3 as tusd
import numpy as np

# Load USD
stage = tusd.Stage.load_from_file("mesh.usd")

# Get mesh data (when implemented)
# positions = np.asarray(mesh.get_points())  # Zero-copy!

# Process with NumPy
# bbox = positions.min(axis=0), positions.max(axis=0)
# transformed = positions @ rotation_matrix.T
```

## Performance Tips

1. Use buffer protocol (automatic with `np.asarray()`)
2. Avoid copying data
3. Reuse objects instead of creating new ones
4. Profile your code

## Support

- Documentation: See `.md` files in this directory
- Issues: File on GitHub
- Examples: Check `examples/` directory
