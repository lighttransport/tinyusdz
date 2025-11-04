#!/usr/bin/env python3
"""
OpenUSD Validation Test for Crate-Writer Output

This script validates USDC files written by crate-writer using the OpenUSD Python API.
It performs comprehensive checks on file format, structure, and data integrity.

Usage:
    source ../../aousd/setup_env_monolithic.sh
    python3 test_openusd_validation.py <usdc_file>
"""

import sys
import os
from pathlib import Path

try:
    from pxr import Usd, UsdGeom, Sdf, Tf
except ImportError:
    print("ERROR: OpenUSD Python bindings not found!")
    print("Please run: source ../../aousd/setup_env_monolithic.sh")
    sys.exit(1)

class USDCValidator:
    """Validates USDC files written by crate-writer"""

    def __init__(self, filepath):
        self.filepath = Path(filepath)
        self.stage = None
        self.layer = None
        self.errors = []
        self.warnings = []
        self.passed_tests = []

    def validate_all(self):
        """Run all validation tests"""
        print(f"\n{'='*70}")
        print(f"OpenUSD Validation Test")
        print(f"{'='*70}")
        print(f"File: {self.filepath}")
        print(f"USD Version: {'.'.join(map(str, Usd.GetVersion()))}")
        print(f"{'='*70}\n")

        # Run all tests
        self.test_file_exists()
        self.test_file_format()
        self.test_open_stage()
        if self.stage:
            self.test_layer_info()
            self.test_prim_structure()
            self.test_attributes()
            self.test_metadata()
            self.test_composition()

        # Print summary
        self.print_summary()

        return len(self.errors) == 0

    def test_file_exists(self):
        """Test 1: File exists and is readable"""
        test_name = "File Existence"
        if not self.filepath.exists():
            self.errors.append(f"{test_name}: File not found")
            return False
        if not self.filepath.is_file():
            self.errors.append(f"{test_name}: Not a regular file")
            return False
        self.passed_tests.append(test_name)
        print(f"✓ {test_name}")
        return True

    def test_file_format(self):
        """Test 2: File has USDC magic header"""
        test_name = "USDC Format Header"
        try:
            with open(self.filepath, 'rb') as f:
                header = f.read(8)
                if header != b'PXR-USDC':
                    self.errors.append(f"{test_name}: Invalid magic header (got {header})")
                    return False
            self.passed_tests.append(test_name)
            print(f"✓ {test_name}")
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def test_open_stage(self):
        """Test 3: OpenUSD can open the file"""
        test_name = "Stage Opening"
        try:
            self.stage = Usd.Stage.Open(str(self.filepath))
            if not self.stage:
                self.errors.append(f"{test_name}: Failed to open stage")
                return False
            self.layer = self.stage.GetRootLayer()
            self.passed_tests.append(test_name)
            print(f"✓ {test_name}")
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def test_layer_info(self):
        """Test 4: Layer metadata"""
        test_name = "Layer Metadata"
        try:
            info = []
            info.append(f"  Identifier: {self.layer.identifier}")
            info.append(f"  Format: {self.layer.GetFileFormat().formatId}")
            info.append(f"  Version: {self.layer.version}")

            # Check if it's really a crate file
            if self.layer.GetFileFormat().formatId != 'usdc':
                self.warnings.append(f"{test_name}: Expected 'usdc' format, got '{self.layer.GetFileFormat().formatId}'")

            self.passed_tests.append(test_name)
            print(f"✓ {test_name}")
            for line in info:
                print(f"  {line}")
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def test_prim_structure(self):
        """Test 5: Prim hierarchy"""
        test_name = "Prim Structure"
        try:
            prims = list(self.stage.Traverse())
            if not prims:
                self.warnings.append(f"{test_name}: No prims found")

            print(f"✓ {test_name}")
            print(f"  Total prims: {len(prims)}")
            print(f"  Prim hierarchy:")
            for prim in prims[:20]:  # Limit output
                prim_type = prim.GetTypeName() or "(no type)"
                print(f"    {prim.GetPath()} [{prim_type}]")
            if len(prims) > 20:
                print(f"    ... and {len(prims) - 20} more")

            self.passed_tests.append(test_name)
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def test_attributes(self):
        """Test 6: Attribute values"""
        test_name = "Attributes"
        try:
            attr_count = 0
            sample_attrs = []

            for prim in self.stage.Traverse():
                attrs = prim.GetAttributes()
                attr_count += len(attrs)

                for attr in attrs[:5]:  # Sample first 5 attrs per prim
                    value = attr.Get()
                    sample_attrs.append({
                        'path': str(attr.GetPath()),
                        'type': attr.GetTypeName(),
                        'value': str(value)[:50]  # Truncate long values
                    })
                    if len(sample_attrs) >= 10:  # Limit total samples
                        break
                if len(sample_attrs) >= 10:
                    break

            print(f"✓ {test_name}")
            print(f"  Total attributes: {attr_count}")
            if sample_attrs:
                print(f"  Sample attributes:")
                for attr in sample_attrs:
                    print(f"    {attr['path']} ({attr['type']}): {attr['value']}")

            self.passed_tests.append(test_name)
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def test_metadata(self):
        """Test 7: Metadata and custom data"""
        test_name = "Metadata"
        try:
            # Check layer metadata
            layer_meta = []
            if self.layer.HasDefaultPrim():
                layer_meta.append(f"Default prim: {self.layer.defaultPrim}")
            if self.layer.HasTimeCodesPerSecond():
                layer_meta.append(f"Time codes/sec: {self.layer.timeCodesPerSecond}")
            if self.layer.HasFramesPerSecond():
                layer_meta.append(f"Frames/sec: {self.layer.framesPerSecond}")

            print(f"✓ {test_name}")
            if layer_meta:
                print(f"  Layer metadata:")
                for meta in layer_meta:
                    print(f"    {meta}")
            else:
                print(f"  (No layer metadata found)")

            self.passed_tests.append(test_name)
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def test_composition(self):
        """Test 8: Composition arcs (references, payloads)"""
        test_name = "Composition Arcs"
        try:
            ref_count = 0
            payload_count = 0
            variant_count = 0

            for prim in self.stage.Traverse():
                if prim.HasAuthoredReferences():
                    ref_count += 1
                if prim.HasPayload():
                    payload_count += 1
                if prim.HasVariantSets():
                    variant_count += len(prim.GetVariantSets().GetNames())

            print(f"✓ {test_name}")
            print(f"  References: {ref_count}")
            print(f"  Payloads: {payload_count}")
            print(f"  Variant sets: {variant_count}")

            self.passed_tests.append(test_name)
            return True
        except Exception as e:
            self.errors.append(f"{test_name}: {e}")
            return False

    def print_summary(self):
        """Print validation summary"""
        print(f"\n{'='*70}")
        print(f"Validation Summary")
        print(f"{'='*70}")
        print(f"Passed: {len(self.passed_tests)}")
        print(f"Warnings: {len(self.warnings)}")
        print(f"Errors: {len(self.errors)}")

        if self.warnings:
            print(f"\nWarnings:")
            for warning in self.warnings:
                print(f"  ⚠ {warning}")

        if self.errors:
            print(f"\nErrors:")
            for error in self.errors:
                print(f"  ✗ {error}")

        print(f"{'='*70}")
        if self.errors:
            print(f"RESULT: FAILED ✗")
        else:
            print(f"RESULT: PASSED ✓")
        print(f"{'='*70}\n")


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 test_openusd_validation.py <usdc_file>")
        print("\nExample:")
        print("  source ../../aousd/setup_env_monolithic.sh")
        print("  python3 test_openusd_validation.py example_output.usdc")
        sys.exit(1)

    filepath = sys.argv[1]
    validator = USDCValidator(filepath)
    success = validator.validate_all()

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
