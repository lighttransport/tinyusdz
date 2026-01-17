#!/usr/bin/env python3
"""
NumPy integration example for TinyUSDZ ABI3 binding

This example demonstrates:
1. Buffer protocol for zero-copy array access
2. NumPy interoperability
3. Efficient array operations
"""

import sys
import os

# Add parent directory to path to import the module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

try:
    import numpy as np
except ImportError:
    print("Error: NumPy is required for this example")
    print("Install with: pip install numpy")
    sys.exit(1)

try:
    import tinyusdz_abi3 as tusd
except ImportError as e:
    print(f"Error: Could not import tinyusdz_abi3: {e}")
    print("\nPlease build the module first:")
    print("  python3 setup.py build_ext --inplace")
    sys.exit(1)


def example_buffer_protocol():
    """Demonstrate buffer protocol support"""
    print("=" * 60)
    print("Example: Buffer Protocol")
    print("=" * 60)

    # Note: In a real implementation, ValueArray would be obtained from
    # USD attributes like positions, normals, etc.
    # For this example, we'll demonstrate the concept

    print("Buffer protocol allows zero-copy array access")
    print("This means NumPy can directly access TinyUSDZ array data")
    print("without copying, making it very efficient.")
    print()

    # Example of how it would work with real data:
    print("Example usage (when fully implemented):")
    print("-" * 40)
    print("stage = tusd.Stage.load_from_file('mesh.usd')")
    print("mesh_prim = stage.get_prim_at_path('/World/Mesh')")
    print("positions = mesh_prim.get_attribute('points').get()")
    print("positions_np = np.asarray(positions)  # Zero-copy!")
    print("print(positions_np.shape)  # (num_points, 3)")
    print("print(positions_np.dtype)  # float32 or float64")
    print()


def example_array_operations():
    """Demonstrate array operations with NumPy"""
    print("=" * 60)
    print("Example: Array Operations")
    print("=" * 60)

    # Simulate mesh positions data
    print("Simulating mesh positions data...")
    positions = np.array([
        [-1.0, -1.0, -1.0],
        [1.0, -1.0, -1.0],
        [1.0, 1.0, -1.0],
        [-1.0, 1.0, -1.0],
        [-1.0, -1.0, 1.0],
        [1.0, -1.0, 1.0],
        [1.0, 1.0, 1.0],
        [-1.0, 1.0, 1.0],
    ], dtype=np.float32)

    print(f"Positions shape: {positions.shape}")
    print(f"Positions dtype: {positions.dtype}")
    print()

    # Compute bounding box
    bbox_min = positions.min(axis=0)
    bbox_max = positions.max(axis=0)
    bbox_size = bbox_max - bbox_min
    bbox_center = (bbox_min + bbox_max) / 2.0

    print("Bounding box:")
    print(f"  Min: {bbox_min}")
    print(f"  Max: {bbox_max}")
    print(f"  Size: {bbox_size}")
    print(f"  Center: {bbox_center}")
    print()

    # Transform operations
    print("Transform operations:")
    scale = 2.0
    translation = np.array([10.0, 0.0, 0.0])

    positions_scaled = positions * scale
    positions_translated = positions + translation
    positions_transformed = positions * scale + translation

    print(f"  Scaled positions (first point): {positions_scaled[0]}")
    print(f"  Translated positions (first point): {positions_translated[0]}")
    print(f"  Transformed positions (first point): {positions_transformed[0]}")
    print()


def example_type_formats():
    """Demonstrate different array type formats"""
    print("=" * 60)
    print("Example: Array Type Formats")
    print("=" * 60)

    print("TinyUSDZ supports various value types with buffer protocol:")
    print()

    formats = [
        ("bool", "?", "Boolean"),
        ("int", "i", "32-bit signed integer"),
        ("uint", "I", "32-bit unsigned integer"),
        ("int64", "q", "64-bit signed integer"),
        ("uint64", "Q", "64-bit unsigned integer"),
        ("float", "f", "32-bit float"),
        ("double", "d", "64-bit float"),
        ("half", "e", "16-bit half-precision float"),
        ("float2", "ff", "2D float vector"),
        ("float3", "fff", "3D float vector (positions, normals, etc.)"),
        ("float4", "ffff", "4D float vector (colors with alpha)"),
    ]

    for type_name, format_str, description in formats:
        print(f"  {type_name:12s} format='{format_str:4s}' - {description}")
    print()

    print("These format strings are compatible with NumPy's dtype system")
    print("and allow zero-copy data access.")
    print()


def example_performance():
    """Demonstrate performance benefits"""
    print("=" * 60)
    print("Example: Performance Benefits")
    print("=" * 60)

    print("Buffer protocol provides significant performance benefits:")
    print()

    # Simulate large mesh
    num_points = 1000000
    positions = np.random.randn(num_points, 3).astype(np.float32)

    print(f"Working with {num_points:,} points (3 MB of data)")
    print()

    # Zero-copy scenario
    print("With buffer protocol (zero-copy):")
    print("  1. TinyUSDZ returns ValueArray")
    print("  2. np.asarray(array) creates view (no copy)")
    print("  3. NumPy operations work directly on original data")
    print("  => Minimal memory overhead, instant access")
    print()

    # Copy scenario
    print("Without buffer protocol (copying):")
    print("  1. TinyUSDZ returns data")
    print("  2. Python creates intermediate list")
    print("  3. NumPy creates array from list (copy)")
    print("  => 2-3x memory overhead, slow for large data")
    print()

    # Memory comparison
    array_size_mb = positions.nbytes / (1024 * 1024)
    print(f"Memory usage comparison for {array_size_mb:.1f} MB array:")
    print(f"  Zero-copy:     {array_size_mb:.1f} MB")
    print(f"  With copying:  {array_size_mb * 2:.1f} MB or more")
    print()


def main():
    print("\n" + "=" * 60)
    print("TinyUSDZ ABI3 Binding - NumPy Integration Examples")
    print("=" * 60 + "\n")

    # Run all examples
    try:
        example_buffer_protocol()
        example_array_operations()
        example_type_formats()
        example_performance()
    except Exception as e:
        print(f"\nError running examples: {e}")
        import traceback
        traceback.print_exc()
        return 1

    print("\n" + "=" * 60)
    print("All examples completed successfully!")
    print("=" * 60 + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
