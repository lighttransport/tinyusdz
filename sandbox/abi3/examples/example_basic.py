#!/usr/bin/env python3
"""
Basic usage example for TinyUSDZ ABI3 binding

This example demonstrates:
1. Loading USD files
2. Creating values
3. Creating prims
4. Memory management (automatic via ref counting)
"""

import sys
import os

# Add parent directory to path to import the module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

try:
    import tinyusdz_abi3 as tusd
except ImportError as e:
    print(f"Error: Could not import tinyusdz_abi3: {e}")
    print("\nPlease build the module first:")
    print("  python3 setup.py build_ext --inplace")
    sys.exit(1)


def example_values():
    """Demonstrate value creation and access"""
    print("=" * 60)
    print("Example: Values")
    print("=" * 60)

    # Create integer value
    val_int = tusd.Value.from_int(42)
    print(f"Integer value type: {val_int.type}")
    print(f"Integer value: {val_int.as_int()}")
    print(f"String representation: {val_int.to_string()}")
    print()

    # Create float value
    val_float = tusd.Value.from_float(3.14159)
    print(f"Float value type: {val_float.type}")
    print(f"Float value: {val_float.as_float()}")
    print(f"String representation: {val_float.to_string()}")
    print()


def example_prims():
    """Demonstrate prim creation"""
    print("=" * 60)
    print("Example: Prims")
    print("=" * 60)

    # Create different types of prims
    prim_types = ["Xform", "Mesh", "Sphere", "Material"]

    for prim_type in prim_types:
        try:
            prim = tusd.Prim(prim_type)
            print(f"Created {prim_type} prim")
            print(f"  Type: {prim.type}")
            # print(f"  String: {prim.to_string()}")
        except Exception as e:
            print(f"Error creating {prim_type}: {e}")
    print()


def example_stage_creation():
    """Demonstrate stage creation"""
    print("=" * 60)
    print("Example: Stage Creation")
    print("=" * 60)

    # Create empty stage
    stage = tusd.Stage()
    print("Created empty stage")
    # print(f"Stage contents:\n{stage.to_string()}")
    print()


def example_stage_loading():
    """Demonstrate loading USD files"""
    print("=" * 60)
    print("Example: Stage Loading")
    print("=" * 60)

    # Try to find a test USD file
    test_files = [
        "../../../models/suzanne.usdc",
        "../../../models/cube.usda",
        "test.usd",
    ]

    for test_file in test_files:
        if os.path.exists(test_file):
            print(f"Loading: {test_file}")
            try:
                stage = tusd.Stage.load_from_file(test_file)
                print("Successfully loaded!")
                # print(f"Stage contents:\n{stage.to_string()}")
                break
            except Exception as e:
                print(f"Error loading: {e}")
    else:
        print("No test USD files found")
    print()


def example_detect_format():
    """Demonstrate format detection"""
    print("=" * 60)
    print("Example: Format Detection")
    print("=" * 60)

    test_filenames = [
        "model.usd",
        "scene.usda",
        "geometry.usdc",
        "archive.usdz",
        "unknown.txt",
    ]

    for filename in test_filenames:
        fmt = tusd.detect_format(filename)
        print(f"{filename:20s} -> {fmt}")
    print()


def example_memory_management():
    """Demonstrate memory management"""
    print("=" * 60)
    print("Example: Memory Management")
    print("=" * 60)

    print("Creating multiple objects...")

    # Create many objects - they should be automatically freed
    for i in range(1000):
        stage = tusd.Stage()
        prim = tusd.Prim("Xform")
        val = tusd.Value.from_int(i)
        # Objects are automatically freed when they go out of scope

    print("Created and freed 1000 sets of objects")
    print("Memory is managed automatically via reference counting")
    print()


def main():
    print("\n" + "=" * 60)
    print("TinyUSDZ ABI3 Binding - Basic Examples")
    print("=" * 60 + "\n")

    # Run all examples
    try:
        example_values()
        example_prims()
        example_stage_creation()
        example_detect_format()
        example_memory_management()
        # example_stage_loading()  # Uncomment if you have test files
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
