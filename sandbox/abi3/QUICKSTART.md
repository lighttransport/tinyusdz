# TinyUSDZ ABI3 Binding - Quick Start Guide

## Prerequisites

- Python 3.10 or later
- C++14 compiler (gcc, clang, or MSVC)
- `uv` package manager (recommended) or pip

## Installation Options

### Option 1: Automated Setup with uv (Recommended)

The easiest way to get started:

```bash
cd sandbox/abi3

# Complete setup: create venv, install deps, build, and test
./setup_env.sh

# Activate the environment
source .venv/bin/activate
```

### Option 2: Using Makefile

```bash
cd sandbox/abi3

# Create environment and install dependencies
make env

# Activate environment
source .venv/bin/activate

# Build the module
make build

# Run tests
make test

# Run examples
make examples
```

### Option 3: Manual Setup

```bash
cd sandbox/abi3

# Install uv if not already installed
curl -LsSf https://astral.sh/uv/install.sh | sh
# or: pip install uv

# Create virtual environment
uv venv .venv

# Activate it
source .venv/bin/activate  # Linux/macOS
# or: .venv\Scripts\activate  # Windows

# Install dependencies
uv pip install numpy setuptools wheel

# Build the module
python setup.py build_ext --inplace

# Run tests
python tests/test_basic.py
```

## Running Examples

### Basic Example

```bash
python examples/example_basic.py
```

This demonstrates:
- Creating Stage, Prim, and Value objects
- Memory management (automatic via ref counting)
- Format detection
- Type conversions

### NumPy Integration Example

```bash
python examples/example_numpy.py
```

This demonstrates:
- Buffer protocol for zero-copy arrays
- NumPy interoperability
- Performance benefits
- Array type formats

### Mesh to NumPy Example

```bash
# With a USD file
python examples/example_mesh_to_numpy.py path/to/mesh.usd

# With synthetic data (no file needed)
python examples/example_mesh_to_numpy.py
```

This demonstrates:
- Loading GeomMesh from USD
- Extracting points, indices, normals, UVs
- Converting to NumPy arrays
- Computing mesh statistics
- Bounding box calculations
- NumPy operations on geometry data

## Installing uv

If you don't have `uv` installed:

### Linux/macOS

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### With pip

```bash
pip install uv
```

### With cargo (Rust)

```bash
cargo install uv
```

## Verifying Installation

After setup, verify everything works:

```python
python -c "import tinyusdz_abi3 as tusd; print(f'✓ TinyUSDZ ABI3 {tusd.__version__}')"
python -c "import numpy as np; print(f'✓ NumPy {np.__version__}')"
```

## Building a Wheel

To create a distributable wheel:

```bash
# Using build script
./build.sh wheel

# Using Makefile
make wheel

# Using setup.py directly
python setup.py bdist_wheel

# Install the wheel
pip install dist/tinyusdz_abi3-*.whl
```

The wheel will be tagged as `cp310-abi3` meaning it works with Python 3.10+.

## Common Issues

### "uv: command not found"

Install uv as shown above.

### "ImportError: No module named 'tinyusdz_abi3'"

Make sure you've built the module:

```bash
python setup.py build_ext --inplace
```

And you're in the right directory or the module is in your Python path.

### Build errors about missing headers

Make sure you have a C++ compiler installed:

- **Linux**: `sudo apt install build-essential` (Debian/Ubuntu)
- **macOS**: `xcode-select --install`
- **Windows**: Install Visual Studio with C++ tools

### NumPy import error

Install NumPy:

```bash
uv pip install numpy
# or
pip install numpy
```

## Quick Reference

### Environment Management

```bash
# Create environment
uv venv .venv

# Activate
source .venv/bin/activate

# Install package
uv pip install <package>

# Deactivate
deactivate
```

### Build Commands

```bash
# Build in-place (for development)
python setup.py build_ext --inplace

# Build wheel (for distribution)
python setup.py bdist_wheel

# Clean build artifacts
make clean
# or
./build.sh clean
```

### Running Code

```bash
# Activate environment first
source .venv/bin/activate

# Run examples
python examples/example_basic.py
python examples/example_numpy.py
python examples/example_mesh_to_numpy.py

# Run tests
python tests/test_basic.py

# Your own code
python my_script.py
```

## Next Steps

1. **Explore the examples** to see what's possible
2. **Read DESIGN.md** to understand the architecture
3. **Check README.md** for detailed API documentation
4. **Write your own scripts** using the binding

## Getting Help

- Check the documentation in `README.md` and `DESIGN.md`
- Look at the examples in `examples/`
- Review the test cases in `tests/`
- File issues on the GitHub repository

## Performance Tips

1. **Use buffer protocol** for large arrays (automatic with NumPy)
2. **Avoid copying** data when possible
3. **Reuse objects** instead of creating new ones in loops
4. **Profile your code** to find bottlenecks

Example of efficient code:

```python
import tinyusdz_abi3 as tusd
import numpy as np

# Load once
stage = tusd.Stage.load_from_file("large_scene.usd")

# Get mesh data (zero-copy via buffer protocol)
mesh = stage.get_prim_at_path("/World/Mesh")
positions = np.asarray(mesh.get_points())  # No copy!

# NumPy operations are fast
bbox_min = positions.min(axis=0)
bbox_max = positions.max(axis=0)

# Transform in-place when possible
positions *= 2.0  # Faster than creating new array
```

## Troubleshooting

### Module built but can't import

Make sure you're in the right directory:

```bash
cd sandbox/abi3
python -c "import tinyusdz_abi3"
```

### Different Python versions

This module requires Python 3.10+. Check your version:

```bash
python --version
```

If you have multiple Python versions:

```bash
python3.10 -m venv .venv
source .venv/bin/activate
```

### Build succeeds but runtime errors

This usually means:
1. Missing TinyUSDZ C++ library
2. Linking issues
3. Missing dependencies

Try rebuilding from scratch:

```bash
make clean
make build
```

## Support

For questions or issues:
1. Check existing documentation
2. Search closed issues
3. Open a new issue with details about your environment

Happy coding!
