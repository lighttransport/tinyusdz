#!/usr/bin/env python3
"""
Basic tests for TinyUSDZ ABI3 binding

Run with: python3 test_basic.py
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


class TestRunner:
    """Simple test runner"""

    def __init__(self):
        self.passed = 0
        self.failed = 0

    def test(self, name, func):
        """Run a test function"""
        try:
            print(f"Running: {name}...", end=" ")
            func()
            print("PASS")
            self.passed += 1
        except Exception as e:
            print(f"FAIL: {e}")
            self.failed += 1
            import traceback
            traceback.print_exc()

    def summary(self):
        """Print test summary"""
        total = self.passed + self.failed
        print("\n" + "=" * 60)
        print(f"Tests: {total}, Passed: {self.passed}, Failed: {self.failed}")
        print("=" * 60)
        return 0 if self.failed == 0 else 1


def test_module_import():
    """Test module can be imported"""
    assert hasattr(tusd, 'Stage'), "Module should have Stage class"
    assert hasattr(tusd, 'Prim'), "Module should have Prim class"
    assert hasattr(tusd, 'Value'), "Module should have Value class"
    assert hasattr(tusd, 'ValueArray'), "Module should have ValueArray class"
    assert hasattr(tusd, 'detect_format'), "Module should have detect_format function"


def test_stage_creation():
    """Test stage creation"""
    stage = tusd.Stage()
    assert stage is not None, "Stage should be created"


def test_prim_creation():
    """Test prim creation"""
    prim = tusd.Prim("Xform")
    assert prim is not None, "Prim should be created"
    assert prim.type == "Xform", f"Prim type should be 'Xform', got '{prim.type}'"


def test_prim_types():
    """Test different prim types"""
    prim_types = ["Xform", "Mesh", "Sphere", "Camera"]
    for prim_type in prim_types:
        prim = tusd.Prim(prim_type)
        assert prim is not None, f"Should create {prim_type} prim"
        assert prim.type == prim_type, f"Prim type should be '{prim_type}'"


def test_value_int():
    """Test integer value"""
    val = tusd.Value.from_int(42)
    assert val is not None, "Value should be created"
    assert val.type == "int", f"Value type should be 'int', got '{val.type}'"
    result = val.as_int()
    assert result == 42, f"Value should be 42, got {result}"


def test_value_float():
    """Test float value"""
    val = tusd.Value.from_float(3.14)
    assert val is not None, "Value should be created"
    assert val.type == "float", f"Value type should be 'float', got '{val.type}'"
    result = val.as_float()
    assert abs(result - 3.14) < 0.001, f"Value should be ~3.14, got {result}"


def test_detect_format():
    """Test format detection"""
    assert tusd.detect_format("test.usda") == "USDA"
    assert tusd.detect_format("test.usdc") == "USDC"
    assert tusd.detect_format("test.usdz") == "USDZ"
    assert tusd.detect_format("test.usd") == "AUTO"


def test_memory_management():
    """Test memory management doesn't crash"""
    # Create and destroy many objects
    for i in range(100):
        stage = tusd.Stage()
        prim = tusd.Prim("Xform")
        val = tusd.Value.from_int(i)
        # Objects should be automatically freed


def test_value_to_string():
    """Test value to_string method"""
    val = tusd.Value.from_int(42)
    s = val.to_string()
    assert isinstance(s, str), "to_string should return string"
    assert len(s) > 0, "String should not be empty"


def test_prim_to_string():
    """Test prim to_string method"""
    prim = tusd.Prim("Xform")
    s = prim.to_string()
    assert isinstance(s, str), "to_string should return string"
    # Note: May be empty for a new prim, which is okay


def test_stage_to_string():
    """Test stage to_string method"""
    stage = tusd.Stage()
    s = stage.to_string()
    assert isinstance(s, str), "to_string should return string"


def test_value_array_creation():
    """Test value array creation"""
    arr = tusd.ValueArray()
    assert arr is not None, "ValueArray should be created"


def test_module_version():
    """Test module has version"""
    assert hasattr(tusd, '__version__'), "Module should have __version__"
    assert isinstance(tusd.__version__, str), "Version should be string"


def main():
    print("\n" + "=" * 60)
    print("TinyUSDZ ABI3 Binding - Basic Tests")
    print("=" * 60 + "\n")

    runner = TestRunner()

    # Run all tests
    runner.test("Module import", test_module_import)
    runner.test("Module version", test_module_version)
    runner.test("Stage creation", test_stage_creation)
    runner.test("Prim creation", test_prim_creation)
    runner.test("Prim types", test_prim_types)
    runner.test("Value integer", test_value_int)
    runner.test("Value float", test_value_float)
    runner.test("Detect format", test_detect_format)
    runner.test("Memory management", test_memory_management)
    runner.test("Value to_string", test_value_to_string)
    runner.test("Prim to_string", test_prim_to_string)
    runner.test("Stage to_string", test_stage_to_string)
    runner.test("ValueArray creation", test_value_array_creation)

    return runner.summary()


if __name__ == "__main__":
    sys.exit(main())
